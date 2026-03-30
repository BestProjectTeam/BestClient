# BestClient Indicator Server

This directory now contains two processes managed by one wrapper:

- `bestclient-indicator-server` in C++: UDP presence + `users.json` writer
- `server.js` in Node.js: HTTPS frontend for `GET /`, `GET /users.json` and `GET /token.json`

Data flow:

`BestClient client -> HTTPS :8779/token.json -> UDP :8778 -> C++ daemon -> users.json -> Node.js HTTPS :8779/users.json -> server browser`

Default ports:

- UDP presence: `8778`
- HTTPS web: `8779`

## Requirements

- C++ daemon built in `build/bestclient-indicator-server`
- Node.js installed and available as `node`
- `openssl` available for shared token and self-signed TLS generation

## Quick start

```bash
cd ~/BestClient
chmod +x src/clientindicatorsrv/run.sh
./src/clientindicatorsrv/run.sh start
./src/clientindicatorsrv/run.sh status
```

HTTPS check:

```bash
curl -k https://127.0.0.1:8779/healthz
curl -k https://127.0.0.1:8779/
curl -k https://127.0.0.1:8779/users.json
curl -k https://127.0.0.1:8779/token.json
```

If no BestClient user is currently registered, `/users.json` returning `[]` is expected.

Stop:

```bash
./src/clientindicatorsrv/run.sh stop
```

## Generated files

- token: `run/clientindicatorsrv/shared-token.txt`
- exported JSON: `run/clientindicatorsrv/users.json`
- certificate: `run/clientindicatorsrv/tls/cert.pem`
- private key: `run/clientindicatorsrv/tls/key.pem`
- supervisor pid: `run/clientindicatorsrv/bestclient-indicator-supervisor.pid`
- UDP pid: `run/clientindicatorsrv/bestclient-indicator-udp.pid`
- web pid: `run/clientindicatorsrv/bestclient-indicator-web.pid`
- supervisor log: `run/clientindicatorsrv/bestclient-indicator-supervisor.log`
- UDP log: `run/clientindicatorsrv/bestclient-indicator-udp.log`
- web log: `run/clientindicatorsrv/bestclient-indicator-web.log`

## Manual setup

Generate the shared token:

```bash
cd ~/BestClient
mkdir -p run/clientindicatorsrv
openssl rand -hex 32 > run/clientindicatorsrv/shared-token.txt
chmod 600 run/clientindicatorsrv/shared-token.txt
```

This token is also served publicly through `GET /token.json`.

The wrapper also generates a self-signed TLS certificate in `run/clientindicatorsrv/tls/` if it does not exist.

Build the UDP daemon:

```bash
cd ~/BestClient
cmake --build build --target bestclient-indicator-server -j"$(nproc)"
```

Run only the UDP daemon manually:

```bash
cd ~/BestClient

./build/bestclient-indicator-server \
  --shared-token "$(cat /root/BestClient/run/clientindicatorsrv/shared-token.txt)" \
  --json-path /root/BestClient/run/clientindicatorsrv/users.json \
  --udp-bind 0.0.0.0:8778
```

Run only the Node web manually:

```bash
cd ~/BestClient/src/clientindicatorsrv
HOST=0.0.0.0 PORT=8779 \
JSON_PATH=/root/BestClient/run/clientindicatorsrv/users.json \
TOKEN_PATH=/root/BestClient/run/clientindicatorsrv/shared-token.txt \
TLS_CERT_FILE=/root/BestClient/run/clientindicatorsrv/tls/cert.pem \
TLS_KEY_FILE=/root/BestClient/run/clientindicatorsrv/tls/key.pem \
node server.js
```

Read the current JSON directly from disk:

```bash
cat ~/BestClient/run/clientindicatorsrv/users.json
```

## Wrapper commands

Use only `run.sh` for manual lifecycle:

```bash
cd ~/BestClient

./src/clientindicatorsrv/run.sh run
./src/clientindicatorsrv/run.sh start
./src/clientindicatorsrv/run.sh stop
./src/clientindicatorsrv/run.sh restart
./src/clientindicatorsrv/run.sh status
tail -f run/clientindicatorsrv/bestclient-indicator-udp.log
tail -f run/clientindicatorsrv/bestclient-indicator-web.log
```

`run.sh run` is foreground mode for `systemd`.
`run.sh start` is background mode for manual use.

## systemd

Install the bundled unit:

```bash
cp ~/BestClient/src/clientindicatorsrv/bestclient-indicator-server.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable bestclient-indicator-server
systemctl start bestclient-indicator-server
systemctl status bestclient-indicator-server
journalctl -u bestclient-indicator-server -f
```

`systemctl stop bestclient-indicator-server` stops both the UDP daemon and the Node web process because `run.sh run` is the supervisor.

## Client config

Recommended client values:

```txt
bc_client_indicator_server_address 150.241.70.188:8778
bc_client_indicator_browser_url https://150.241.70.188:8779/users.json
bc_client_indicator_token_url https://150.241.70.188:8779/token.json
bc_client_indicator_shared_token <optional fallback token>
```

Legacy saved values `http://150.241.70.188:8779/users.json` and `http://150.241.70.188:8779/token.json` are auto-migrated by the client to the HTTPS defaults on startup. Custom URLs are left unchanged.

The client first tries `/token.json`, then falls back to `bc_client_indicator_shared_token` if the web token is unavailable or invalid.

The Node web-server uses a self-signed certificate by default. The client already disables peer verification for these requests, so this works without importing the certificate into the OS trust store.

## Troubleshooting

If `curl -k https://127.0.0.1:8779/users.json` works but returns `[]`:

- the web server is fine;
- no client has registered yet over UDP;
- `https://127.0.0.1:8779/` is also a friendly alias for `/users.json`;
- verify the client token matches `shared-token.txt`;
- verify `curl -k https://127.0.0.1:8779/token.json` returns a token;
- verify `bc_client_indicator 1`;
- verify the client uses `150.241.70.188:8778`;
- verify UDP port `8778` is reachable through your firewall.

If `run.sh start` fails:

```bash
./src/clientindicatorsrv/run.sh status
tail -n 100 run/clientindicatorsrv/bestclient-indicator-supervisor.log
tail -n 100 run/clientindicatorsrv/bestclient-indicator-udp.log
tail -n 100 run/clientindicatorsrv/bestclient-indicator-web.log
```

If you need to restart everything cleanly:

```bash
./src/clientindicatorsrv/run.sh stop
./src/clientindicatorsrv/run.sh start
```
