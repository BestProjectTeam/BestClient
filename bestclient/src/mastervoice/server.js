'use strict';

const fs = require('fs/promises');
const fsSync = require('fs');
const http = require('http');
const https = require('https');
const path = require('path');

const HOST = process.env.HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.PORT || '3000', 10);
const USE_HTTPS = process.env.HTTPS !== '0';
const TLS_CERT_FILE = process.env.TLS_CERT_FILE || path.join(__dirname, 'certs', 'localhost-cert.pem');
const TLS_KEY_FILE = process.env.TLS_KEY_FILE || path.join(__dirname, 'certs', 'localhost-key.pem');
const SERVERS_FILE = path.join(__dirname, 'servers.json');

function sendJson(res, statusCode, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(body),
  });
  res.end(body);
}

async function loadServers() {
  const raw = await fs.readFile(SERVERS_FILE, 'utf8');
  const parsed = JSON.parse(raw);
  if(!Array.isArray(parsed)) {
    throw new Error('servers.json must contain a JSON array');
  }

  return parsed.map((entry) => ({
    name: typeof entry.name === 'string' ? entry.name : '',
    address: typeof entry.address === 'string' ? entry.address : '',
    flag: Number.isInteger(entry.flag) ? entry.flag : 0,
  })).filter((entry) => entry.name.length > 0 && entry.address.length > 0);
}

const requestListener = async (req, res) => {
  if(req.method === 'GET' && req.url === '/healthz') {
    sendJson(res, 200, { ok: true });
    return;
  }

  if(req.method === 'GET' && req.url === '/voice/servers.json') {
    try {
      const servers = await loadServers();
      sendJson(res, 200, servers);
    } catch(error) {
      sendJson(res, 500, {
        error: 'failed_to_load_servers',
        message: error instanceof Error ? error.message : String(error),
      });
    }
    return;
  }

  sendJson(res, 404, { error: 'not_found' });
};

function createServer() {
  if(!USE_HTTPS) {
    return {
      server: http.createServer(requestListener),
      protocol: 'http',
    };
  }

  let key;
  let cert;
  try {
    key = fsSync.readFileSync(TLS_KEY_FILE);
    cert = fsSync.readFileSync(TLS_CERT_FILE);
  } catch(error) {
    console.error('failed to read TLS certificate files');
    console.error(`key:  ${TLS_KEY_FILE}`);
    console.error(`cert: ${TLS_CERT_FILE}`);
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }

  return {
    server: https.createServer({ key, cert }, requestListener),
    protocol: 'https',
  };
}

const { server, protocol } = createServer();

server.listen(PORT, HOST, () => {
  console.log(`mastervoice listening on ${protocol}://${HOST}:${PORT}`);
});
