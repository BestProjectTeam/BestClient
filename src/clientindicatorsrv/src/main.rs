use constant_time_eq::constant_time_eq;
use hmac::{Hmac, Mac};
use hyper::server::conn::Http;
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::convert::Infallible;
use std::env;
use std::fs;
use std::io;
use std::net::{IpAddr, SocketAddr};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::net::{TcpListener, UdpSocket};
use tokio::time;
use tokio_rustls::{rustls::ServerConfig, TlsAcceptor};
use warp::Filter;

const PROTOCOL_MAGIC: [u8; 4] = *b"BCI1";
const PROTOCOL_VERSION: u8 = 1;
const DEFAULT_UDP_BIND: &str = "0.0.0.0:8778";
const DEFAULT_WEB_HOST: &str = "0.0.0.0";
const DEFAULT_WEB_PORT: u16 = 8779;
const MAX_UDP_PACKET_SIZE: usize = 2048;
const PROOF_SIZE: usize = 32;
const HMAC_SIZE: usize = 32;
const NONCE_RETENTION: Duration = Duration::from_secs(60);
const HEARTBEAT_TIMEOUT: Duration = Duration::from_secs(15);
const CLEANUP_INTERVAL: Duration = Duration::from_secs(1);
const MIN_PRESENCE_INTERVAL: Duration = Duration::from_millis(200);
const MIN_DEV_AUTH_INTERVAL: Duration = Duration::from_secs(1);
const MIN_VERSION_INTERVAL: Duration = Duration::from_secs(1);
const MAX_ENTRIES: usize = 5000;
const MAX_ENTRIES_PER_IP: usize = 4;
const MAX_NONCES: usize = 20000;
const MAX_BROADCAST_PEERS: usize = 128;
const DEFAULT_TLS_HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(5);
const DEFAULT_HTTP_HEADER_TIMEOUT: Duration = Duration::from_secs(5);
const DEFAULT_HTTPS_CONNECTION_TIMEOUT: Duration = Duration::from_secs(15);

#[derive(Clone)]
struct Config {
    udp_bind: SocketAddr,
    web_bind: SocketAddr,
    shared_token: String,
    dev_secret: String,
    json_path: PathBuf,
    tls_cert_file: PathBuf,
    tls_key_file: PathBuf,
    tls_handshake_timeout: Duration,
    http_header_timeout: Duration,
    https_connection_timeout: Duration,
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
struct IdentityKey {
    instance_id: [u8; 16],
    client_id: i16,
}

#[derive(Clone)]
struct PresencePacket {
    packet_type: u8,
    instance_id: [u8; 16],
    nonce: [u8; 16],
    timestamp: u64,
    server_address: String,
    player_name: String,
    client_id: i16,
}

#[derive(Clone)]
struct VersionPacket {
    instance_id: [u8; 16],
    nonce: [u8; 16],
    timestamp: u64,
    server_address: String,
    player_name: String,
    client_id: i16,
    client_version: String,
}

#[derive(Clone)]
struct PendingVersionEntry {
    remote_addr: SocketAddr,
    server_address: String,
    player_name: String,
    client_version: Option<String>,
}

#[derive(Clone)]
struct PresenceEntry {
    identity: IdentityKey,
    server_address: String,
    player_name: String,
    remote_addr: SocketAddr,
    last_seen: Instant,
    last_seen_unix: u64,
    developer: bool,
    client_version: Option<String>,
}

#[derive(Clone)]
struct OutPacket {
    to: SocketAddr,
    data: Vec<u8>,
}

#[derive(Clone)]
struct TokenBucket {
    tokens: f64,
    last: Instant,
    capacity: f64,
    refill_per_second: f64,
}

impl TokenBucket {
    fn new(now: Instant, capacity: f64, refill_per_second: f64) -> Self {
        Self {
            tokens: capacity,
            last: now,
            capacity,
            refill_per_second,
        }
    }

    fn allow(&mut self, now: Instant) -> bool {
        let elapsed = now.saturating_duration_since(self.last).as_secs_f64();
        self.last = now;
        self.tokens = (self.tokens + elapsed * self.refill_per_second).min(self.capacity);
        if self.tokens < 1.0 {
            return false;
        }
        self.tokens -= 1.0;
        true
    }
}

struct ServerState {
    entries: HashMap<IdentityKey, PresenceEntry>,
    pending_versions: HashMap<IdentityKey, PendingVersionEntry>,
    recent_nonces: HashMap<([u8; 16], [u8; 16]), Instant>,
    last_presence_by_identity: HashMap<IdentityKey, Instant>,
    last_dev_auth_by_identity: HashMap<IdentityKey, Instant>,
    last_version_by_identity: HashMap<IdentityKey, Instant>,
    rate_by_ip: HashMap<IpAddr, TokenBucket>,
    invalid_rate_by_ip: HashMap<IpAddr, TokenBucket>,
    last_invalid_log_by_ip: HashMap<IpAddr, Instant>,
    json_path: PathBuf,
}

impl ServerState {
    fn new(json_path: PathBuf) -> Self {
        Self {
            entries: HashMap::new(),
            pending_versions: HashMap::new(),
            recent_nonces: HashMap::new(),
            last_presence_by_identity: HashMap::new(),
            last_dev_auth_by_identity: HashMap::new(),
            last_version_by_identity: HashMap::new(),
            rate_by_ip: HashMap::new(),
            invalid_rate_by_ip: HashMap::new(),
            last_invalid_log_by_ip: HashMap::new(),
            json_path,
        }
    }

    fn allow_ip(&mut self, ip: IpAddr, now: Instant) -> bool {
        self.rate_by_ip
            .entry(ip)
            .or_insert_with(|| TokenBucket::new(now, 240.0, 120.0))
            .allow(now)
    }

    fn allow_invalid(&mut self, ip: IpAddr, now: Instant) -> bool {
        self.invalid_rate_by_ip
            .entry(ip)
            .or_insert_with(|| TokenBucket::new(now, 10.0, 2.0))
            .allow(now)
    }

    fn remember_nonce(&mut self, instance_id: [u8; 16], nonce: [u8; 16], now: Instant) -> bool {
        if self.recent_nonces.len() >= MAX_NONCES {
            self.cleanup_nonces(now);
            if self.recent_nonces.len() >= MAX_NONCES {
                return false;
            }
        }
        let key = (instance_id, nonce);
        if let Some(seen) = self.recent_nonces.get(&key) {
            if now.saturating_duration_since(*seen) <= NONCE_RETENTION {
                return false;
            }
        }
        self.recent_nonces.insert(key, now);
        true
    }

    fn cleanup_nonces(&mut self, now: Instant) {
        self.recent_nonces
            .retain(|_, seen| now.saturating_duration_since(*seen) <= NONCE_RETENTION);
    }

    fn log_invalid(&mut self, ip: IpAddr, now: Instant, reason: &str) {
        let should_log = self
            .last_invalid_log_by_ip
            .get(&ip)
            .map_or(true, |last| now.saturating_duration_since(*last) >= Duration::from_secs(10));
        if should_log {
            eprintln!("invalid clientindicator packet from {ip}: {reason}");
            self.last_invalid_log_by_ip.insert(ip, now);
        }
    }

    fn handle_presence(&mut self, packet: PresencePacket, from: SocketAddr, now: Instant) -> (bool, Vec<OutPacket>) {
        let key = IdentityKey {
            instance_id: packet.instance_id,
            client_id: packet.client_id,
        };
        if packet.client_id < 0 || packet.client_id > 255 || packet.server_address.is_empty() {
            return (false, Vec::new());
        }
        if packet.packet_type != PACKET_LEAVE
            && self
                .last_presence_by_identity
                .get(&key)
                .map_or(false, |last| now.saturating_duration_since(*last) < MIN_PRESENCE_INTERVAL)
        {
            return (false, Vec::new());
        }
        self.last_presence_by_identity.insert(key, now);

        match packet.packet_type {
            PACKET_JOIN | PACKET_HEARTBEAT => self.handle_join_or_heartbeat(packet, from, now),
            PACKET_LEAVE => self.handle_leave(key, from),
            _ => (false, Vec::new()),
        }
    }

    fn handle_join_or_heartbeat(&mut self, packet: PresencePacket, from: SocketAddr, now: Instant) -> (bool, Vec<OutPacket>) {
        let identity = IdentityKey {
            instance_id: packet.instance_id,
            client_id: packet.client_id,
        };
        let old = self.entries.get(&identity).cloned();

        if let Some(ref old_entry) = old {
            if old_entry.remote_addr != from {
                return (false, Vec::new());
            }
        } else {
            if self.entries.len() >= MAX_ENTRIES {
                return (false, Vec::new());
            }
            let ip_count = self.entries.values().filter(|e| e.remote_addr.ip() == from.ip()).count();
            if ip_count >= MAX_ENTRIES_PER_IP {
                return (false, Vec::new());
            }
        }

        let treat_as_join = packet.packet_type == PACKET_JOIN || old.is_none();
        let old_server = old.as_ref().map(|entry| entry.server_address.clone());
        let old_name = old.as_ref().map(|entry| entry.player_name.clone());
        let old_developer = old.as_ref().map_or(false, |entry| entry.developer);
        let pending_client_version = self.pending_versions.get(&identity).and_then(|entry| {
            (entry.remote_addr == from
                && entry.server_address == packet.server_address
                && entry.player_name == packet.player_name)
                .then(|| entry.client_version.clone())
                .flatten()
        });
        let old_client_version = old
            .as_ref()
            .and_then(|entry| entry.client_version.clone())
            .or(pending_client_version);
        let entry = PresenceEntry {
            identity,
            server_address: packet.server_address,
            player_name: packet.player_name,
            remote_addr: from,
            last_seen: now,
            last_seen_unix: unix_timestamp(),
            developer: old_developer,
            client_version: old_client_version,
        };
        let server_changed = old_server.as_deref().is_some_and(|server| server != entry.server_address);
        let name_changed = old_name.as_deref().is_some_and(|name| name != entry.player_name);
        let mut out = Vec::new();

        if let Some(old_entry) = old {
            if server_changed {
                out.extend(self.broadcast_peer_state(&old_entry, PACKET_PEER_REMOVE, None, Some(identity)));
            }
        }

        self.entries.insert(identity, entry.clone());
        self.pending_versions.remove(&identity);

        if treat_as_join || server_changed {
            out.push(self.peer_list_for(&entry));
            out.push(self.peer_dev_list_for(&entry));
            out.extend(self.peer_version_states_for(&entry));
        }
        if treat_as_join || server_changed || name_changed {
            out.extend(self.broadcast_peer_state(&entry, PACKET_PEER_STATE, Some(entry.remote_addr), Some(identity)));
            if entry.developer {
                out.extend(self.broadcast_peer_dev_state(&entry, true, Some(entry.remote_addr), Some(identity)));
            }
            if entry.client_version.is_some() {
                out.extend(self.broadcast_peer_version_state(&entry, Some(entry.remote_addr), Some(identity)));
            }
        }

        (treat_as_join || server_changed || name_changed, out)
    }

    fn handle_leave(&mut self, key: IdentityKey, from: SocketAddr) -> (bool, Vec<OutPacket>) {
        if let Some(entry) = self.entries.get(&key) {
            if entry.remote_addr != from {
                return (false, Vec::new());
            }
        }
        let Some(entry) = self.entries.remove(&key) else {
            return (false, Vec::new());
        };
        self.last_presence_by_identity.remove(&key);
        self.last_dev_auth_by_identity.remove(&key);
        self.last_version_by_identity.remove(&key);
        self.pending_versions.remove(&key);
        let out = self.broadcast_peer_state(&entry, PACKET_PEER_REMOVE, None, Some(key));
        (true, out)
    }

    fn handle_dev_auth(
        &mut self,
        packet: PresencePacket,
        from: SocketAddr,
        now: Instant,
        success: bool,
    ) -> (bool, Vec<OutPacket>) {
        let key = IdentityKey {
            instance_id: packet.instance_id,
            client_id: packet.client_id,
        };
        if self
            .last_dev_auth_by_identity
            .get(&key)
            .map_or(false, |last| now.saturating_duration_since(*last) < MIN_DEV_AUTH_INTERVAL)
        {
            return (false, Vec::new());
        }
        self.last_dev_auth_by_identity.insert(key, now);

        let mut out = vec![OutPacket {
            to: from,
            data: write_dev_auth_result(&packet.server_address, packet.client_id, success),
        }];
        if !success {
            return (false, out);
        }

        let Some(entry) = self.entries.get_mut(&key) else {
            return (false, out);
        };
        if entry.server_address != packet.server_address {
            return (false, out);
        }
        if entry.remote_addr != from {
            return (false, out);
        }

        entry.developer = true;
        let entry = entry.clone();
        out.extend(self.broadcast_peer_dev_state(&entry, true, Some(from), Some(key)));
        (true, out)
    }

    fn handle_version(
        &mut self,
        packet: VersionPacket,
        from: SocketAddr,
        now: Instant,
    ) -> (bool, Vec<OutPacket>) {
        let key = IdentityKey {
            instance_id: packet.instance_id,
            client_id: packet.client_id,
        };
        if self
            .last_version_by_identity
            .get(&key)
            .map_or(false, |last| now.saturating_duration_since(*last) < MIN_VERSION_INTERVAL)
        {
            return (false, Vec::new());
        }
        self.last_version_by_identity.insert(key, now);

        let client_version = sanitize_client_version(&packet.client_version);

        let Some(entry) = self.entries.get_mut(&key) else {
            self.pending_versions.insert(
                key,
                PendingVersionEntry {
                    remote_addr: from,
                    server_address: packet.server_address,
                    player_name: packet.player_name,
                    client_version,
                },
            );
            return (false, Vec::new());
        };
        if entry.server_address != packet.server_address
            || entry.player_name != packet.player_name
            || entry.remote_addr != from
        {
            self.pending_versions.insert(
                key,
                PendingVersionEntry {
                    remote_addr: from,
                    server_address: packet.server_address,
                    player_name: packet.player_name,
                    client_version,
                },
            );
            return (false, Vec::new());
        }

        if entry.client_version == client_version {
            return (false, Vec::new());
        }

        entry.client_version = client_version;
        let entry = entry.clone();
        let out = self.broadcast_peer_version_state(&entry, None, Some(key));
        (true, out)
    }

    fn cleanup(&mut self, now: Instant) -> (bool, Vec<OutPacket>) {
        let mut removed = Vec::new();
        self.entries.retain(|key, entry| {
            let expired = now.saturating_duration_since(entry.last_seen) > HEARTBEAT_TIMEOUT;
            if expired {
                removed.push((*key, entry.clone()));
            }
            !expired
        });
        for (key, _) in &removed {
            self.last_presence_by_identity.remove(key);
            self.last_dev_auth_by_identity.remove(key);
            self.last_version_by_identity.remove(key);
            self.pending_versions.remove(key);
        }
        self.cleanup_nonces(now);
        self.rate_by_ip
            .retain(|_, bucket| now.saturating_duration_since(bucket.last) <= Duration::from_secs(120));
        self.invalid_rate_by_ip
            .retain(|_, bucket| now.saturating_duration_since(bucket.last) <= Duration::from_secs(120));
        self.last_invalid_log_by_ip
            .retain(|_, last| now.saturating_duration_since(*last) <= Duration::from_secs(120));

        let mut out = Vec::new();
        for (key, entry) in removed {
            out.extend(self.broadcast_peer_state(&entry, PACKET_PEER_REMOVE, None, Some(key)));
        }
        (!out.is_empty(), out)
    }

    fn broadcast_peer_state(
        &self,
        entry: &PresenceEntry,
        packet_type: u8,
        except: Option<SocketAddr>,
        self_key: Option<IdentityKey>,
    ) -> Vec<OutPacket> {
        let data = write_peer_state(packet_type, &entry.server_address, &entry.player_name, entry.identity.client_id);
        self.broadcast_to_server(entry, except, self_key, data)
    }

    fn broadcast_peer_dev_state(
        &self,
        entry: &PresenceEntry,
        developer: bool,
        except: Option<SocketAddr>,
        self_key: Option<IdentityKey>,
    ) -> Vec<OutPacket> {
        let data = write_peer_dev_state(&entry.server_address, &entry.player_name, entry.identity.client_id, developer);
        self.broadcast_to_server(entry, except, self_key, data)
    }

    fn broadcast_peer_version_state(
        &self,
        entry: &PresenceEntry,
        except: Option<SocketAddr>,
        self_key: Option<IdentityKey>,
    ) -> Vec<OutPacket> {
        let Some(client_version) = entry.client_version.as_deref() else {
            return Vec::new();
        };
        let data = write_peer_version_state(&entry.server_address, &entry.player_name, entry.identity.client_id, client_version);
        self.broadcast_to_server(entry, except, self_key, data)
    }

    fn broadcast_to_server(
        &self,
        entry: &PresenceEntry,
        except: Option<SocketAddr>,
        self_key: Option<IdentityKey>,
        data: Vec<u8>,
    ) -> Vec<OutPacket> {
        let mut out = Vec::new();
        for (key, other) in &self.entries {
            if Some(*key) == self_key
                || other.server_address != entry.server_address
                || other.identity.instance_id == entry.identity.instance_id
                || Some(other.remote_addr) == except
            {
                continue;
            }
            out.push(OutPacket {
                to: other.remote_addr,
                data: data.clone(),
            });
            if out.len() >= MAX_BROADCAST_PEERS {
                break;
            }
        }
        out
    }

    fn peer_list_for(&self, recipient: &PresenceEntry) -> OutPacket {
        let mut client_ids: Vec<i16> = self
            .entries
            .values()
            .filter(|entry| {
                entry.identity != recipient.identity
                    && entry.server_address == recipient.server_address
                    && entry.identity.instance_id != recipient.identity.instance_id
            })
            .map(|entry| entry.identity.client_id)
            .collect();
        client_ids.sort_unstable();
        OutPacket {
            to: recipient.remote_addr,
            data: write_peer_list(PACKET_PEER_LIST, &recipient.server_address, &client_ids),
        }
    }

    fn peer_dev_list_for(&self, recipient: &PresenceEntry) -> OutPacket {
        let mut client_ids: Vec<i16> = self
            .entries
            .values()
            .filter(|entry| {
                entry.developer
                    && entry.identity != recipient.identity
                    && entry.server_address == recipient.server_address
                    && entry.identity.instance_id != recipient.identity.instance_id
            })
            .map(|entry| entry.identity.client_id)
            .collect();
        client_ids.sort_unstable();
        OutPacket {
            to: recipient.remote_addr,
            data: write_peer_list(PACKET_PEER_DEV_LIST, &recipient.server_address, &client_ids),
        }
    }

    fn peer_version_states_for(&self, recipient: &PresenceEntry) -> Vec<OutPacket> {
        let mut out = Vec::new();
        for entry in self.entries.values() {
            if entry.identity == recipient.identity
                || entry.server_address != recipient.server_address
                || entry.identity.instance_id == recipient.identity.instance_id
            {
                continue;
            }
            let Some(client_version) = entry.client_version.as_deref() else {
                continue;
            };
            out.push(OutPacket {
                to: recipient.remote_addr,
                data: write_peer_version_state(&entry.server_address, &entry.player_name, entry.identity.client_id, client_version),
            });
            if out.len() >= MAX_BROADCAST_PEERS {
                break;
            }
        }
        out
    }

    fn snapshot_json(&self) -> String {
        let mut servers: HashMap<&str, Vec<&PresenceEntry>> = HashMap::new();
        for entry in self.entries.values() {
            servers.entry(&entry.server_address).or_default().push(entry);
        }

        let mut server_addresses: Vec<&str> = servers.keys().copied().collect();
        server_addresses.sort_unstable();
        let mut snapshot = Vec::new();
        for server_address in server_addresses {
            let mut players = servers.remove(server_address).unwrap_or_default();
            players.sort_by(|left, right| {
                left.player_name
                    .cmp(&right.player_name)
                    .then(left.identity.client_id.cmp(&right.identity.client_id))
            });
            snapshot.push(ServerSnapshot {
                server_address,
                players: players
                    .into_iter()
                    .map(|entry| PlayerSnapshot {
                        name: &entry.player_name,
                        client_id: entry.identity.client_id,
                        instance_id: format_uuid(entry.identity.instance_id),
                        last_seen: entry.last_seen_unix,
                        developer: entry.developer.then_some(true),
                        version: entry.client_version.as_deref(),
                    })
                    .collect(),
            });
        }
        serde_json::to_string(&snapshot).unwrap_or_else(|_| "[]".to_string())
    }

    fn write_snapshot(&self) -> io::Result<()> {
        write_file_atomically(&self.json_path, &self.snapshot_json())
    }
}

#[derive(Serialize)]
struct ServerSnapshot<'a> {
    server_address: &'a str,
    players: Vec<PlayerSnapshot<'a>>,
}

#[derive(Serialize)]
struct PlayerSnapshot<'a> {
    name: &'a str,
    client_id: i16,
    instance_id: String,
    last_seen: u64,
    #[serde(skip_serializing_if = "Option::is_none")]
    developer: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    version: Option<&'a str>,
}

const PACKET_JOIN: u8 = 1;
const PACKET_HEARTBEAT: u8 = 2;
const PACKET_LEAVE: u8 = 3;
const PACKET_PEER_STATE: u8 = 4;
const PACKET_PEER_REMOVE: u8 = 5;
const PACKET_PEER_LIST: u8 = 6;
const PACKET_DEV_AUTH: u8 = 7;
const PACKET_PEER_DEV_STATE: u8 = 8;
const PACKET_PEER_DEV_LIST: u8 = 9;
const PACKET_VERSION_ANNOUNCE: u8 = 11;
const PACKET_PEER_VERSION_STATE: u8 = 12;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let _ = dotenvy::from_filename("src/clientindicatorsrv/.env");
    let _ = dotenvy::dotenv();
    let config = Config::load()?;
    let state = Arc::new(Mutex::new(ServerState::new(config.json_path.clone())));
    {
        let state = state.lock().unwrap();
        let _ = state.write_snapshot();
    }

    let udp_socket = Arc::new(UdpSocket::bind(config.udp_bind).await?);
    eprintln!("clientindicator UDP listening on {}", config.udp_bind);
    eprintln!("clientindicator HTTPS listening on {}", config.web_bind);

    let udp_task = tokio::spawn(udp_loop(
        Arc::clone(&udp_socket),
        Arc::clone(&state),
        config.shared_token.clone(),
        config.dev_secret.clone(),
    ));
    let cleanup_task = tokio::spawn(cleanup_loop(Arc::clone(&udp_socket), Arc::clone(&state)));
    let web_task = tokio::spawn(web_loop(Arc::clone(&state), config.clone()));

    tokio::select! {
        result = udp_task => result??,
        result = cleanup_task => result??,
        result = web_task => result??,
        _ = tokio::signal::ctrl_c() => {},
    }
    Ok(())
}

impl Config {
    fn load() -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        let state_dir = env::var("STATE_DIR").unwrap_or_else(|_| "run/clientindicatorsrv".to_string());
        let token_path = env::var("TOKEN_PATH").unwrap_or_else(|_| format!("{state_dir}/shared-token.txt"));
        let shared_token = env::var("BC_CLIENT_INDICATOR_SHARED_TOKEN")
            .ok()
            .filter(|token| !token.trim().is_empty())
            .unwrap_or_else(|| fs::read_to_string(&token_path).unwrap_or_default())
            .trim()
            .to_string();
        if shared_token.is_empty() {
            return Err("BC_CLIENT_INDICATOR_SHARED_TOKEN or TOKEN_PATH must provide a non-empty shared token".into());
        }

        let dev_secret = env::var("BC_CLIENT_INDICATOR_SECRET_KEY")
            .or_else(|_| env::var("BcClientIndicatorSecretKey"))
            .unwrap_or_default()
            .trim()
            .to_string();
        let udp_bind = env::var("UDP_BIND").unwrap_or_else(|_| DEFAULT_UDP_BIND.to_string()).parse()?;
        let web_host = env::var("WEB_HOST").unwrap_or_else(|_| DEFAULT_WEB_HOST.to_string());
        let web_port: u16 = env::var("WEB_PORT")
            .ok()
            .and_then(|value| value.parse().ok())
            .unwrap_or(DEFAULT_WEB_PORT);
        let web_bind = format!("{web_host}:{web_port}").parse()?;
        let json_path = env::var("JSON_PATH").unwrap_or_else(|_| format!("{state_dir}/users.json")).into();
        let tls_cert_file = env::var("TLS_CERT_FILE")
            .unwrap_or_else(|_| format!("{state_dir}/tls/cert.pem"))
            .into();
        let tls_key_file = env::var("TLS_KEY_FILE")
            .unwrap_or_else(|_| format!("{state_dir}/tls/key.pem"))
            .into();
        let tls_handshake_timeout =
            env_duration_ms("WEB_TLS_HANDSHAKE_TIMEOUT_MS", DEFAULT_TLS_HANDSHAKE_TIMEOUT);
        let http_header_timeout =
            env_duration_ms("WEB_HTTP_HEADER_TIMEOUT_MS", DEFAULT_HTTP_HEADER_TIMEOUT);
        let https_connection_timeout =
            env_duration_ms("WEB_HTTPS_CONNECTION_TIMEOUT_MS", DEFAULT_HTTPS_CONNECTION_TIMEOUT);
        Ok(Self {
            udp_bind,
            web_bind,
            shared_token,
            dev_secret,
            json_path,
            tls_cert_file,
            tls_key_file,
            tls_handshake_timeout,
            http_header_timeout,
            https_connection_timeout,
        })
    }
}

fn env_duration_ms(name: &str, default: Duration) -> Duration {
    env::var(name)
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .map(Duration::from_millis)
        .unwrap_or(default)
}

async fn udp_loop(
    socket: Arc<UdpSocket>,
    state: Arc<Mutex<ServerState>>,
    shared_token: String,
    dev_secret: String,
) -> io::Result<()> {
    let mut buf = [0u8; MAX_UDP_PACKET_SIZE];
    loop {
        let (size, from) = socket.recv_from(&mut buf).await?;
        let data = &buf[..size];
        let out = {
            let mut state = state.lock().unwrap();
            handle_udp_packet(&mut state, &shared_token, &dev_secret, from, data)
        };
        for packet in out {
            let _ = socket.send_to(&packet.data, packet.to).await;
        }
    }
}

fn handle_udp_packet(
    state: &mut ServerState,
    shared_token: &str,
    dev_secret: &str,
    from: SocketAddr,
    data: &[u8],
) -> Vec<OutPacket> {
    let now = Instant::now();
    let ip = from.ip();
    if data.len() > MAX_UDP_PACKET_SIZE || !state.allow_ip(ip, now) {
        return Vec::new();
    }
    let Some(packet_type) = packet_type(data) else {
        state.log_invalid(ip, now, "bad header");
        return Vec::new();
    };

    if matches!(packet_type, PACKET_JOIN | PACKET_HEARTBEAT | PACKET_LEAVE) {
        let Some(packet) = read_presence_packet(data, &[PACKET_JOIN, PACKET_HEARTBEAT, PACKET_LEAVE], PROOF_SIZE) else {
            state.log_invalid(ip, now, "bad presence payload");
            return Vec::new();
        };
        if !validate_proof(shared_token, data) {
            if state.allow_invalid(ip, now) {
                state.log_invalid(ip, now, "invalid presence proof");
            }
            return Vec::new();
        }
        if !state.remember_nonce(packet.instance_id, packet.nonce, now) {
            state.log_invalid(ip, now, "nonce replay");
            return Vec::new();
        }
        let _timestamp = packet.timestamp;
        let (dirty, out) = state.handle_presence(packet, from, now);
        if dirty {
            let _ = state.write_snapshot();
        }
        return out;
    }

    if packet_type == PACKET_DEV_AUTH {
        let Some(packet) = read_presence_packet(data, &[PACKET_DEV_AUTH], HMAC_SIZE) else {
            state.log_invalid(ip, now, "bad dev auth payload");
            return Vec::new();
        };
        let success = !dev_secret.is_empty() && validate_hmac(dev_secret, data);
        if !success && state.allow_invalid(ip, now) {
            state.log_invalid(ip, now, "invalid dev auth");
        }
        if !state.remember_nonce(packet.instance_id, packet.nonce, now) {
            state.log_invalid(ip, now, "dev auth nonce replay");
            return Vec::new();
        }
        let _timestamp = packet.timestamp;
        let (dirty, out) = state.handle_dev_auth(packet, from, now, success);
        if dirty {
            let _ = state.write_snapshot();
        }
        return out;
    }

    if packet_type == PACKET_VERSION_ANNOUNCE {
        let Some(packet) = read_version_packet(data, PROOF_SIZE) else {
            state.log_invalid(ip, now, "bad version payload");
            return Vec::new();
        };
        if !validate_proof(shared_token, data) {
            if state.allow_invalid(ip, now) {
                state.log_invalid(ip, now, "invalid version proof");
            }
            return Vec::new();
        }
        if !state.remember_nonce(packet.instance_id, packet.nonce, now) {
            state.log_invalid(ip, now, "version nonce replay");
            return Vec::new();
        }
        let _timestamp = packet.timestamp;
        let (dirty, out) = state.handle_version(packet, from, now);
        if dirty {
            let _ = state.write_snapshot();
        }
        return out;
    }

    Vec::new()
}

async fn cleanup_loop(socket: Arc<UdpSocket>, state: Arc<Mutex<ServerState>>) -> io::Result<()> {
    let mut interval = time::interval(CLEANUP_INTERVAL);
    loop {
        interval.tick().await;
        let out = {
            let mut state = state.lock().unwrap();
            let (dirty, out) = state.cleanup(Instant::now());
            if dirty {
                let _ = state.write_snapshot();
            }
            out
        };
        for packet in out {
            let _ = socket.send_to(&packet.data, packet.to).await;
        }
    }
}

async fn web_loop(
    state: Arc<Mutex<ServerState>>,
    config: Config,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let healthz = warp::path("healthz")
        .and(warp::get())
        .map(|| warp::reply::json(&serde_json::json!({ "ok": true })));

    let users_state = Arc::clone(&state);
    let users = warp::path::end()
        .or(warp::path("users.json").and(warp::path::end()))
        .and(warp::get())
        .and_then(move |_| {
            let users_state = Arc::clone(&users_state);
            async move {
                let body = users_state.lock().unwrap().snapshot_json();
                Ok::<_, Infallible>(warp::reply::with_header(
                    body,
                    "content-type",
                    "application/json; charset=utf-8",
                ))
            }
        });

    let routes = healthz
        .or(users)
        .with(warp::reply::with::header("cache-control", "no-store"))
        .with(warp::reply::with::header("connection", "close"));
    let tls_acceptor = load_tls_acceptor(&config)?;
    let listener = TcpListener::bind(config.web_bind).await?;

    loop {
        let (stream, peer_addr) = listener.accept().await?;
        let _ = stream.set_nodelay(true);

        let routes = routes.clone();
        let tls_acceptor = tls_acceptor.clone();
        let tls_handshake_timeout = config.tls_handshake_timeout;
        let http_header_timeout = config.http_header_timeout;
        let https_connection_timeout = config.https_connection_timeout;

        tokio::spawn(async move {
            let tls_stream = match time::timeout(tls_handshake_timeout, tls_acceptor.accept(stream))
                .await
            {
                Ok(Ok(stream)) => stream,
                Ok(Err(err)) => {
                    eprintln!("clientindicator HTTPS TLS handshake failed from {peer_addr}: {err}");
                    return;
                }
                Err(_) => {
                    eprintln!("clientindicator HTTPS TLS handshake timeout from {peer_addr}");
                    return;
                }
            };

            let mut http = Http::new();
            http.http1_only(true)
                .http1_keep_alive(false)
                .http1_header_read_timeout(http_header_timeout);

            let service = warp::service(routes);
            match time::timeout(
                https_connection_timeout,
                http.serve_connection(tls_stream, service),
            )
            .await
            {
                Ok(Ok(())) => {}
                Ok(Err(err)) => {
                    eprintln!("clientindicator HTTPS request failed from {peer_addr}: {err}");
                }
                Err(_) => eprintln!("clientindicator HTTPS request timeout from {peer_addr}"),
            }
        });
    }
}

fn load_tls_acceptor(
    config: &Config,
) -> Result<TlsAcceptor, Box<dyn std::error::Error + Send + Sync>> {
    let cert_file = fs::File::open(&config.tls_cert_file)?;
    let mut cert_reader = io::BufReader::new(cert_file);
    let certs = rustls_pemfile::certs(&mut cert_reader).collect::<Result<Vec<_>, _>>()?;
    if certs.is_empty() {
        return Err(format!(
            "no certificates found in {}",
            config.tls_cert_file.display()
        )
        .into());
    }

    let key_file = fs::File::open(&config.tls_key_file)?;
    let mut key_reader = io::BufReader::new(key_file);
    let Some(key) = rustls_pemfile::private_key(&mut key_reader)? else {
        return Err(format!(
            "no private key found in {}",
            config.tls_key_file.display()
        )
        .into());
    };

    let tls_config = ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certs, key)?;
    Ok(TlsAcceptor::from(Arc::new(tls_config)))
}

fn packet_type(data: &[u8]) -> Option<u8> {
    if data.len() < 6 || data[..4] != PROTOCOL_MAGIC || data[5] != PROTOCOL_VERSION {
        return None;
    }
    Some(data[4])
}

fn read_presence_packet(data: &[u8], allowed_types: &[u8], trailer_size: usize) -> Option<PresencePacket> {
    let packet_type = packet_type(data)?;
    if !allowed_types.contains(&packet_type) || data.len() < trailer_size {
        return None;
    }
    let payload_len = data.len().checked_sub(trailer_size)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let instance_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let player_name = reader.string()?;
    let client_id = reader.i16()?;
    if reader.remaining() != 0 {
        return None;
    }
    Some(PresencePacket {
        packet_type,
        instance_id,
        nonce,
        timestamp,
        server_address,
        player_name,
        client_id,
    })
}

fn read_version_packet(data: &[u8], trailer_size: usize) -> Option<VersionPacket> {
    let packet_type = packet_type(data)?;
    if packet_type != PACKET_VERSION_ANNOUNCE || data.len() < trailer_size {
        return None;
    }
    let payload_len = data.len().checked_sub(trailer_size)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let instance_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let player_name = reader.string()?;
    let client_id = reader.i16()?;
    let client_version = reader.string()?;
    if reader.remaining() != 0 {
        return None;
    }
    Some(VersionPacket {
        instance_id,
        nonce,
        timestamp,
        server_address,
        player_name,
        client_id,
        client_version,
    })
}

struct Reader<'a> {
    data: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, offset: 0 }
    }

    fn skip(&mut self, count: usize) -> Option<()> {
        self.offset = self.offset.checked_add(count)?;
        (self.offset <= self.data.len()).then_some(())
    }

    fn bytes(&mut self, count: usize) -> Option<&'a [u8]> {
        let end = self.offset.checked_add(count)?;
        if end > self.data.len() {
            return None;
        }
        let out = &self.data[self.offset..end];
        self.offset = end;
        Some(out)
    }

    fn uuid(&mut self) -> Option<[u8; 16]> {
        self.bytes(16)?.try_into().ok()
    }

    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
    }

    fn i16(&mut self) -> Option<i16> {
        Some(i16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
    }

    fn u64(&mut self) -> Option<u64> {
        Some(u64::from_be_bytes(self.bytes(8)?.try_into().ok()?))
    }

    fn string(&mut self) -> Option<String> {
        let len = self.u16()? as usize;
        let bytes = self.bytes(len)?;
        Some(String::from_utf8_lossy(bytes).into_owned())
    }

    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.offset)
    }
}

fn write_header(out: &mut Vec<u8>, packet_type: u8) {
    out.extend_from_slice(&PROTOCOL_MAGIC);
    out.push(packet_type);
    out.push(PROTOCOL_VERSION);
}

fn write_string(out: &mut Vec<u8>, value: &str) {
    let bytes = value.as_bytes();
    let len = bytes.len().min(u16::MAX as usize);
    out.extend_from_slice(&(len as u16).to_be_bytes());
    out.extend_from_slice(&bytes[..len]);
}

fn write_peer_state(packet_type: u8, server_address: &str, player_name: &str, client_id: i16) -> Vec<u8> {
    let mut out = Vec::with_capacity(128);
    write_header(&mut out, packet_type);
    write_string(&mut out, server_address);
    write_string(&mut out, player_name);
    out.extend_from_slice(&client_id.to_be_bytes());
    out
}

fn write_peer_dev_state(server_address: &str, player_name: &str, client_id: i16, developer: bool) -> Vec<u8> {
    let mut out = write_peer_state(PACKET_PEER_DEV_STATE, server_address, player_name, client_id);
    out.push(u8::from(developer));
    out
}

fn write_peer_version_state(server_address: &str, player_name: &str, client_id: i16, client_version: &str) -> Vec<u8> {
    let mut out = write_peer_state(PACKET_PEER_VERSION_STATE, server_address, player_name, client_id);
    write_string(&mut out, client_version);
    out
}

fn write_peer_list(packet_type: u8, server_address: &str, client_ids: &[i16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(128);
    write_header(&mut out, packet_type);
    write_string(&mut out, server_address);
    out.extend_from_slice(&(client_ids.len().min(u16::MAX as usize) as u16).to_be_bytes());
    for client_id in client_ids.iter().take(u16::MAX as usize) {
        out.extend_from_slice(&client_id.to_be_bytes());
    }
    out
}

fn write_dev_auth_result(server_address: &str, client_id: i16, success: bool) -> Vec<u8> {
    let mut out = Vec::with_capacity(64);
    write_header(&mut out, 10);
    write_string(&mut out, server_address);
    out.extend_from_slice(&client_id.to_be_bytes());
    out.push(u8::from(success));
    out
}

fn sanitize_client_version(value: &str) -> Option<String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return None;
    }

    if trimmed.len() > 8 {
        return None;
    }

    let mut has_digit = false;
    for ch in trimmed.chars() {
        if ch.is_ascii_digit() {
            has_digit = true;
            continue;
        }
        if ch == '.' {
            continue;
        }
        return None;
    }

    has_digit.then(|| trimmed.to_string())
}

fn validate_proof(shared_token: &str, data: &[u8]) -> bool {
    if data.len() < PROOF_SIZE {
        return false;
    }
    let payload_len = data.len() - PROOF_SIZE;
    let mut sha = Sha256::new();
    sha.update(shared_token.as_bytes());
    sha.update(&data[..payload_len]);
    let expected = sha.finalize();
    constant_time_eq(&expected, &data[payload_len..])
}

fn validate_hmac(secret: &str, data: &[u8]) -> bool {
    if secret.is_empty() || data.len() < HMAC_SIZE {
        return false;
    }
    let payload_len = data.len() - HMAC_SIZE;
    let Ok(mut mac) = Hmac::<Sha256>::new_from_slice(secret.as_bytes()) else {
        return false;
    };
    mac.update(&data[..payload_len]);
    mac.verify_slice(&data[payload_len..]).is_ok()
}

fn format_uuid(uuid: [u8; 16]) -> String {
    format!(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]
    )
}

fn unix_timestamp() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn write_file_atomically(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = path.with_extension("tmp");
    fs::write(&tmp, contents)?;
    fs::rename(tmp, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_presence_packet(packet_type: u8, secret: &str) -> Vec<u8> {
        let mut out = Vec::new();
        write_header(&mut out, packet_type);
        out.extend_from_slice(&[1; 16]);
        out.extend_from_slice(&[2; 16]);
        out.extend_from_slice(&123u64.to_be_bytes());
        write_string(&mut out, "127.0.0.1:8303");
        write_string(&mut out, "dev");
        out.extend_from_slice(&4i16.to_be_bytes());
        if packet_type == PACKET_DEV_AUTH {
            let mut mac = Hmac::<Sha256>::new_from_slice(secret.as_bytes()).unwrap();
            mac.update(&out);
            out.extend_from_slice(&mac.finalize().into_bytes());
        } else {
            let mut sha = Sha256::new();
            sha.update(secret.as_bytes());
            sha.update(&out);
            out.extend_from_slice(&sha.finalize());
        }
        out
    }

    fn write_version_packet(secret: &str, version: &str) -> Vec<u8> {
        let mut out = Vec::new();
        write_header(&mut out, PACKET_VERSION_ANNOUNCE);
        out.extend_from_slice(&[1; 16]);
        out.extend_from_slice(&[2; 16]);
        out.extend_from_slice(&123u64.to_be_bytes());
        write_string(&mut out, "127.0.0.1:8303");
        write_string(&mut out, "dev");
        out.extend_from_slice(&4i16.to_be_bytes());
        write_string(&mut out, version);
        let mut sha = Sha256::new();
        sha.update(secret.as_bytes());
        sha.update(&out);
        out.extend_from_slice(&sha.finalize());
        out
    }

    #[test]
    fn parses_v1_presence_packet() {
        let packet = write_presence_packet(PACKET_JOIN, "shared");
        let parsed = read_presence_packet(&packet, &[PACKET_JOIN], PROOF_SIZE).unwrap();
        assert_eq!(parsed.packet_type, PACKET_JOIN);
        assert_eq!(parsed.client_id, 4);
        assert_eq!(parsed.player_name, "dev");
        assert!(validate_proof("shared", &packet));
    }

    #[test]
    fn rejects_invalid_presence_proof() {
        let packet = write_presence_packet(PACKET_JOIN, "shared");
        assert!(!validate_proof("wrong", &packet));
    }

    #[test]
    fn accepts_dev_auth_only_with_matching_secret() {
        let packet = write_presence_packet(PACKET_DEV_AUTH, "dev-secret");
        assert!(validate_hmac("dev-secret", &packet));
        assert!(!validate_hmac("wrong", &packet));
        assert!(!validate_hmac("", &packet));
    }

    #[test]
    fn nonce_replay_is_rejected() {
        let mut state = ServerState::new(PathBuf::from("/tmp/clientindicator-test.json"));
        let now = Instant::now();
        assert!(state.remember_nonce([1; 16], [2; 16], now));
        assert!(!state.remember_nonce([1; 16], [2; 16], now));
    }

    #[test]
    fn rate_limiter_blocks_burst_and_refills() {
        let now = Instant::now();
        let mut bucket = TokenBucket::new(now, 2.0, 1.0);
        assert!(bucket.allow(now));
        assert!(bucket.allow(now));
        assert!(!bucket.allow(now));
        assert!(bucket.allow(now + Duration::from_secs(1)));
    }

    #[test]
    fn snapshot_serializes_developer_only_when_true() {
        let mut state = ServerState::new(PathBuf::from("/tmp/clientindicator-test.json"));
        state.entries.insert(
            IdentityKey {
                instance_id: [1; 16],
                client_id: 1,
            },
            PresenceEntry {
                identity: IdentityKey {
                    instance_id: [1; 16],
                    client_id: 1,
                },
                server_address: "127.0.0.1:8303".to_string(),
                player_name: "dev".to_string(),
                remote_addr: "127.0.0.1:12345".parse().unwrap(),
                last_seen: Instant::now(),
                last_seen_unix: 10,
                developer: true,
                client_version: Some("1.7.1".to_string()),
            },
        );
        let json = state.snapshot_json();
        assert!(json.contains("\"developer\":true"));
        assert!(json.contains("\"version\":\"1.7.1\""));
    }

    #[test]
    fn parses_version_packet() {
        let packet = write_version_packet("shared", "1.7.1");
        let parsed = read_version_packet(&packet, PROOF_SIZE).unwrap();
        assert!(validate_proof("shared", &packet));
        assert_eq!(parsed.client_id, 4);
        assert_eq!(parsed.player_name, "dev");
        assert_eq!(parsed.client_version, "1.7.1");
    }

    #[test]
    fn version_arriving_before_join_is_preserved() {
        let now = Instant::now();
        let mut state = ServerState::new(PathBuf::from("/tmp/clientindicator-test.json"));

        let version_dirty = state.handle_version(
            VersionPacket {
                instance_id: [1; 16],
                nonce: [2; 16],
                timestamp: 1,
                server_address: "127.0.0.1:8303".to_string(),
                player_name: "dev".to_string(),
                client_id: 4,
                client_version: "1.7.1".to_string(),
            },
            "127.0.0.1:12345".parse().unwrap(),
            now,
        );
        assert!(!version_dirty.0);

        let (dirty, _out) = state.handle_presence(
            PresencePacket {
                packet_type: PACKET_JOIN,
                instance_id: [1; 16],
                nonce: [3; 16],
                timestamp: 2,
                server_address: "127.0.0.1:8303".to_string(),
                player_name: "dev".to_string(),
                client_id: 4,
            },
            "127.0.0.1:12345".parse().unwrap(),
            now,
        );
        assert!(dirty);
        let entry = state.entries.get(&IdentityKey {
            instance_id: [1; 16],
            client_id: 4,
        });
        assert_eq!(entry.and_then(|e| e.client_version.as_deref()), Some("1.7.1"));
    }

    #[test]
    fn sanitize_client_version_accepts_only_digits_and_dots_up_to_eight_chars() {
        assert_eq!(sanitize_client_version("1.1.1.1").as_deref(), Some("1.1.1.1"));
        assert_eq!(sanitize_client_version("17.1").as_deref(), Some("17.1"));
        assert_eq!(sanitize_client_version("1.2beta"), None);
        assert_eq!(sanitize_client_version("ad"), None);
        assert_eq!(sanitize_client_version("1.1.1.1a"), None);
        assert_eq!(sanitize_client_version("12.34.56").as_deref(), Some("12.34.56"));
        assert_eq!(sanitize_client_version("123456789"), None);
        assert_eq!(sanitize_client_version("........"), None);
    }
}
