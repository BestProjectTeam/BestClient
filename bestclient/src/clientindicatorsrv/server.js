'use strict';

const fs = require('fs/promises');
const fsSync = require('fs');
const https = require('https');
const path = require('path');

const HOST = process.env.HOST || '0.0.0.0';
const PORT = Number.parseInt(process.env.PORT || '8779', 10);
const JSON_PATH = process.env.JSON_PATH || path.join(__dirname, '..', '..', 'run', 'clientindicatorsrv', 'users.json');
const TOKEN_PATH = process.env.TOKEN_PATH || path.join(__dirname, '..', '..', 'run', 'clientindicatorsrv', 'shared-token.txt');
const TLS_CERT_FILE = process.env.TLS_CERT_FILE || path.join(__dirname, '..', '..', 'run', 'clientindicatorsrv', 'tls', 'cert.pem');
const TLS_KEY_FILE = process.env.TLS_KEY_FILE || path.join(__dirname, '..', '..', 'run', 'clientindicatorsrv', 'tls', 'key.pem');

function writeJson(res, statusCode, body) {
  const payload = JSON.stringify(body);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(payload),
  });
  res.end(payload);
}

function writeRawJson(res, statusCode, payload) {
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(payload),
  });
  res.end(payload);
}

async function loadUsersJson() {
  try {
    const raw = await fs.readFile(JSON_PATH, 'utf8');
    const parsed = JSON.parse(raw);
    if(!Array.isArray(parsed)) {
      throw new Error('users.json must contain a top-level JSON array');
    }
    return raw;
  } catch(error) {
    if(error && error.code === 'ENOENT') {
      return '[]\n';
    }
    throw error;
  }
}

async function loadTokenJson() {
  const raw = await fs.readFile(TOKEN_PATH, 'utf8');
  const token = raw.trim();
  if(token.length === 0) {
    throw new Error('shared-token.txt is empty');
  }
  return { token };
}

const requestListener = async (req, res) => {
  if(req.method === 'GET' && req.url === '/healthz') {
    writeJson(res, 200, { ok: true });
    return;
  }

  if(req.method === 'GET' && (req.url === '/' || req.url === '/users.json')) {
    try {
      const usersJson = await loadUsersJson();
      writeRawJson(res, 200, usersJson);
    } catch(error) {
      writeJson(res, 500, {
        error: 'failed_to_load_users',
        message: error instanceof Error ? error.message : String(error),
      });
    }
    return;
  }

  if(req.method === 'GET' && req.url === '/token.json') {
    try {
      const tokenJson = await loadTokenJson();
      writeJson(res, 200, tokenJson);
    } catch(error) {
      writeJson(res, 500, {
        error: 'failed_to_load_token',
        message: error instanceof Error ? error.message : String(error),
      });
    }
    return;
  }

  writeJson(res, 404, { error: 'not_found' });
};

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

const server = https.createServer({ key, cert }, requestListener);
server.listen(PORT, HOST, () => {
  console.log(`clientindicator-web listening on https://${HOST}:${PORT}`);
  console.log(`serving JSON from ${JSON_PATH}`);
  console.log(`serving token from ${TOKEN_PATH}`);
  console.log(`using cert ${TLS_CERT_FILE}`);
});
