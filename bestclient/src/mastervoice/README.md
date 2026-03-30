# mastervoice

Lightweight Node.js service that serves the voice server list used by the BestClient voice UI.

## Run

```bash
cd src/mastervoice
npm start
```

Service listens on `https://127.0.0.1:3000` by default.
Use another port with:

```bash
PORT=3010 npm start
```

Disable HTTPS only for debugging:

```bash
HTTPS=0 npm start
```

## TLS certificate

By default `server.js` loads:

- `certs/localhost-key.pem`
- `certs/localhost-cert.pem`

To use custom paths:

```bash
TLS_KEY_FILE=/path/to/key.pem TLS_CERT_FILE=/path/to/cert.pem npm start
```

## Endpoints

- `GET /healthz` -> health response
- `GET /voice/servers.json` -> voice server list

## Server list format

Edit `servers.json` in this directory.
Expected format:

```json
[
  {
    "name": "My Voice Server",
    "address": "voice.example.com:8777",
    "flag": 276
  }
]
```
