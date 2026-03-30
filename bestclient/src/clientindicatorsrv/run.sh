#!/usr/bin/env bash
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SELF_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BIN_PATH="${BIN_PATH:-$BUILD_DIR/bestclient-indicator-server}"
NODE_BIN="${NODE_BIN:-node}"

STATE_DIR="${STATE_DIR:-$ROOT_DIR/run/clientindicatorsrv}"
JSON_PATH="${JSON_PATH:-$STATE_DIR/users.json}"
TOKEN_PATH="${TOKEN_PATH:-$STATE_DIR/shared-token.txt}"
TLS_DIR="${TLS_DIR:-$STATE_DIR/tls}"
TLS_CERT_FILE="${TLS_CERT_FILE:-$TLS_DIR/cert.pem}"
TLS_KEY_FILE="${TLS_KEY_FILE:-$TLS_DIR/key.pem}"

UDP_BIND="${UDP_BIND:-0.0.0.0:8778}"
WEB_HOST="${WEB_HOST:-0.0.0.0}"
WEB_PORT="${WEB_PORT:-8779}"
PUBLIC_HOST="${PUBLIC_HOST:-150.241.70.188}"

SUPERVISOR_PID_PATH="${SUPERVISOR_PID_PATH:-$STATE_DIR/bestclient-indicator-supervisor.pid}"
UDP_PID_PATH="${UDP_PID_PATH:-$STATE_DIR/bestclient-indicator-udp.pid}"
WEB_PID_PATH="${WEB_PID_PATH:-$STATE_DIR/bestclient-indicator-web.pid}"

SUPERVISOR_LOG_PATH="${SUPERVISOR_LOG_PATH:-$STATE_DIR/bestclient-indicator-supervisor.log}"
UDP_LOG_PATH="${UDP_LOG_PATH:-$STATE_DIR/bestclient-indicator-udp.log}"
WEB_LOG_PATH="${WEB_LOG_PATH:-$STATE_DIR/bestclient-indicator-web.log}"

MODE="${1:-run}"

mkdir -p "$STATE_DIR"

pid_is_running() {
  local pid_file="$1"
  if [[ ! -s "$pid_file" ]]; then
    return 1
  fi
  local pid
  pid="$(cat "$pid_file")"
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

cleanup_pidfile_if_stale() {
  local pid_file="$1"
  if ! pid_is_running "$pid_file"; then
    rm -f "$pid_file"
  fi
}

ensure_setup() {
  cleanup_pidfile_if_stale "$SUPERVISOR_PID_PATH"
  cleanup_pidfile_if_stale "$UDP_PID_PATH"
  cleanup_pidfile_if_stale "$WEB_PID_PATH"

  mkdir -p "$STATE_DIR" "$TLS_DIR"

  if [[ ! -x "$BIN_PATH" ]]; then
    echo "bestclient-indicator-server not built, building it..."
    cmake --build "$BUILD_DIR" --target bestclient-indicator-server -j"$(nproc)"
  fi

  if [[ ! -s "$TOKEN_PATH" ]]; then
    echo "Generating shared token: $TOKEN_PATH"
    openssl rand -hex 32 > "$TOKEN_PATH"
    chmod 600 "$TOKEN_PATH"
  fi

  if ! command -v "$NODE_BIN" >/dev/null 2>&1; then
    echo "Node.js not found: $NODE_BIN" >&2
    exit 1
  fi

  if [[ ! -s "$TLS_CERT_FILE" || ! -s "$TLS_KEY_FILE" ]]; then
    echo "Generating self-signed TLS certificate for $PUBLIC_HOST"
    openssl req -x509 -newkey rsa:4096 -sha256 -days 3650 -nodes \
      -keyout "$TLS_KEY_FILE" \
      -out "$TLS_CERT_FILE" \
      -subj "/CN=$PUBLIC_HOST" \
      -addext "subjectAltName = IP:$PUBLIC_HOST"
    chmod 600 "$TLS_KEY_FILE"
    chmod 644 "$TLS_CERT_FILE"
  fi
}

print_config() {
  echo "BestClient indicator server"
  echo "  UDP:        $UDP_BIND"
  echo "  Web:        https://$WEB_HOST:$WEB_PORT/users.json"
  echo "  JSON:       $JSON_PATH"
  echo "  Token file: $TOKEN_PATH"
  echo "  Token URL:  https://$WEB_HOST:$WEB_PORT/token.json"
  echo "  TLS cert:   $TLS_CERT_FILE"
  echo "  TLS key:    $TLS_KEY_FILE"
  echo "  UDP PID:    $UDP_PID_PATH"
  echo "  Web PID:    $WEB_PID_PATH"
  echo "  UDP LOG:    $UDP_LOG_PATH"
  echo "  Web LOG:    $WEB_LOG_PATH"
}

start_udp() {
  local shared_token
  shared_token="$(tr -d '\r\n' < "$TOKEN_PATH")"
  : > "$UDP_LOG_PATH"
  "$BIN_PATH" \
    --shared-token "$shared_token" \
    --json-path "$JSON_PATH" \
    --udp-bind "$UDP_BIND" >>"$UDP_LOG_PATH" 2>&1 &
  echo $! > "$UDP_PID_PATH"
}

start_web() {
  : > "$WEB_LOG_PATH"
  JSON_PATH="$JSON_PATH" TOKEN_PATH="$TOKEN_PATH" HOST="$WEB_HOST" PORT="$WEB_PORT" TLS_CERT_FILE="$TLS_CERT_FILE" TLS_KEY_FILE="$TLS_KEY_FILE" "$NODE_BIN" "$SELF_DIR/server.js" >>"$WEB_LOG_PATH" 2>&1 &
  echo $! > "$WEB_PID_PATH"
}

stop_pid() {
  local pid_file="$1"
  if ! pid_is_running "$pid_file"; then
    rm -f "$pid_file"
    return 0
  fi

  local pid
  pid="$(cat "$pid_file")"
  kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 20); do
    if ! kill -0 "$pid" 2>/dev/null; then
      rm -f "$pid_file"
      return 0
    fi
    sleep 1
  done

  kill -KILL "$pid" 2>/dev/null || true
  rm -f "$pid_file"
}

stop_children() {
  stop_pid "$UDP_PID_PATH"
  stop_pid "$WEB_PID_PATH"
}

run_cleanup() {
  trap - INT TERM EXIT
  stop_children
  rm -f "$SUPERVISOR_PID_PATH"
}

run_server() {
  ensure_setup
  if pid_is_running "$UDP_PID_PATH" || pid_is_running "$WEB_PID_PATH"; then
    echo "bestclient-indicator-server is already running" >&2
    exit 1
  fi

  print_config
  echo $$ > "$SUPERVISOR_PID_PATH"

  trap run_cleanup INT TERM EXIT

  start_udp
  start_web

  local udp_pid web_pid status=0
  udp_pid="$(cat "$UDP_PID_PATH")"
  web_pid="$(cat "$WEB_PID_PATH")"

  set +e
  wait -n "$udp_pid" "$web_pid"
  status=$?
  set -e

  if kill -0 "$udp_pid" 2>/dev/null || kill -0 "$web_pid" 2>/dev/null; then
    echo "A child process exited, shutting down the remaining child" >>"$SUPERVISOR_LOG_PATH"
  fi

  exit "$status"
}

start_server() {
  ensure_setup
  if pid_is_running "$SUPERVISOR_PID_PATH" || (pid_is_running "$UDP_PID_PATH" && pid_is_running "$WEB_PID_PATH"); then
    echo "bestclient-indicator-server is already running"
    exit 0
  fi

  cleanup_pidfile_if_stale "$SUPERVISOR_PID_PATH"
  cleanup_pidfile_if_stale "$UDP_PID_PATH"
  cleanup_pidfile_if_stale "$WEB_PID_PATH"

  : > "$SUPERVISOR_LOG_PATH"
  nohup "$0" run >>"$SUPERVISOR_LOG_PATH" 2>&1 &
  echo $! > "$SUPERVISOR_PID_PATH"
  sleep 1

  if pid_is_running "$UDP_PID_PATH" && pid_is_running "$WEB_PID_PATH"; then
    echo "Started bestclient-indicator-server"
    print_config
  else
    echo "Failed to start bestclient-indicator-server" >&2
    exit 1
  fi
}

stop_server() {
  if pid_is_running "$SUPERVISOR_PID_PATH"; then
    local pid
    pid="$(cat "$SUPERVISOR_PID_PATH")"
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do
      if ! kill -0 "$pid" 2>/dev/null; then
        break
      fi
      sleep 1
    done
  fi

  stop_children
  rm -f "$SUPERVISOR_PID_PATH"
  echo "Stopped bestclient-indicator-server"
}

status_server() {
  cleanup_pidfile_if_stale "$SUPERVISOR_PID_PATH"
  cleanup_pidfile_if_stale "$UDP_PID_PATH"
  cleanup_pidfile_if_stale "$WEB_PID_PATH"

  local exit_code=0
  if pid_is_running "$SUPERVISOR_PID_PATH"; then
    echo "Supervisor: running (PID $(cat "$SUPERVISOR_PID_PATH"))"
  else
    echo "Supervisor: stopped"
    exit_code=1
  fi

  if pid_is_running "$UDP_PID_PATH"; then
    echo "UDP daemon: running (PID $(cat "$UDP_PID_PATH"))"
  else
    echo "UDP daemon: stopped"
    exit_code=1
  fi

  if pid_is_running "$WEB_PID_PATH"; then
    echo "Node web:   running (PID $(cat "$WEB_PID_PATH"))"
  else
    echo "Node web:   stopped"
    exit_code=1
  fi

  exit "$exit_code"
}

case "$MODE" in
  run)
    run_server
    ;;
  start)
    start_server
    ;;
  stop)
    stop_server
    ;;
  restart)
    stop_server
    start_server
    ;;
  status)
    status_server
    ;;
  *)
    echo "Usage: $0 [run|start|stop|restart|status]" >&2
    exit 1
    ;;
esac
