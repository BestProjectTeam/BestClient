# BestClient Voice Server (Rust)

Standalone UDP relay server compatible with BestClient voice protocol (`BVC1`, version `2`).

## Build (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y build-essential pkg-config
curl https://sh.rustup.rs -sSf | sh -s -- -y
source ~/.cargo/env

cd bestclient-voice-server
cargo build --release
```

Binary path:

```bash
./target/release/bestclient-voice-server
```

## Run

```bash
./target/release/bestclient-voice-server --bind 0.0.0.0:8777
```

Show all options:

```bash
./target/release/bestclient-voice-server --help
```

## Firewall

Open UDP port (example for `ufw`):

```bash
sudo ufw allow 8777/udp
```

## systemd (auto-start)

Example unit:

```ini
# /etc/systemd/system/bestclient-voice.service
[Unit]
Description=BestClient Voice Server
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/bestclient-voice-server
ExecStart=/opt/bestclient-voice-server/target/release/bestclient-voice-server --bind 0.0.0.0:8777 --timeout-secs 30 --peerlist-interval-secs 5
Restart=always
RestartSec=2

# hardening (optional)
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true

[Install]
WantedBy=multi-user.target
```

Enable + start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now bestclient-voice.service
sudo systemctl status bestclient-voice.service
```

Logs:

```bash
journalctl -u bestclient-voice.service -f
```

## Client connect

In-game:

- `!voice server <your_vps_ip:8777>`
- `!voice on`

