use std::collections::{HashMap, HashSet};
use std::env;
use std::io;
use std::net::{SocketAddr, UdpSocket};
use std::process;
use std::time::{Duration, Instant};

const PROTOCOL_MAGIC: u32 = 0x4256_4331; // 'BVC1'
const PROTOCOL_VERSION: u8 = 3;

const MAX_PACKET_SIZE: usize = 1400;
const DEFAULT_BIND: &str = "0.0.0.0:8777";

const PACKET_HELLO: u8 = 1;
const PACKET_HELLO_ACK: u8 = 2;
const PACKET_VOICE: u8 = 3;
const PACKET_VOICE_RELAY: u8 = 4;
const PACKET_PEER_LIST: u8 = 5;
const PACKET_PING: u8 = 6;
const PACKET_PONG: u8 = 7;
const PACKET_PEER_LIST_EX: u8 = 8;
const PACKET_GOODBYE: u8 = 9;

// Voice groups (capability advertised via PACKET_HELLO_ACK flags).
const PACKET_GROUP_LIST_REQ: u8 = 10;
const PACKET_GROUP_LIST: u8 = 11;
const PACKET_GROUP_CREATE_REQ: u8 = 12;
const PACKET_GROUP_CREATE_ACK: u8 = 13;
const PACKET_GROUP_JOIN_ID_REQ: u8 = 14;
const PACKET_GROUP_JOIN_NAME_REQ: u8 = 15;
const PACKET_GROUP_JOIN_ACK: u8 = 16;
const PACKET_GROUP_SET_PRIVACY_REQ: u8 = 17;
const PACKET_GROUP_SET_PRIVACY_ACK: u8 = 18;
const PACKET_GROUP_INVITE_REQ: u8 = 19;
const PACKET_GROUP_INVITE_EVT: u8 = 20;

const GROUP_NAME_MAX: usize = 128;
const ROOM_KEY_MAX: usize = 128;
const GROUP_EMPTY_TTL: Duration = Duration::from_secs(30 * 60);
const INVITE_TTL: Duration = Duration::from_secs(10 * 60);

const GROUP_PRIVACY_PUBLIC: u8 = 0;
const GROUP_PRIVACY_PRIVATE: u8 = 1;

const GROUP_STATUS_OK: u8 = 0;
const GROUP_STATUS_INVALID: u8 = 2;
const GROUP_STATUS_EXISTS: u8 = 3;
const GROUP_STATUS_NOT_FOUND: u8 = 4;
const GROUP_STATUS_FORBIDDEN: u8 = 5;
const GROUP_STATUS_NOT_INVITED: u8 = 6;

#[derive(Debug, Clone)]
struct Config {
    bind: String,
    timeout: Duration,
    peer_list_interval: Duration,
    max_pps: u32,
    max_bps: u32,
    max_clients: usize,
    quiet: bool,
}

#[derive(Debug)]
enum ParseResult {
    Config(Config),
    Help,
}

#[derive(Debug, Default)]
struct Stats {
    packets_in: u64,
    packets_out: u64,
    packets_drop_parse: u64,
    packets_drop_capacity: u64,
    packets_drop_rate: u64,
    ping_requests: u64,
    hello_packets: u64,
    voice_packets: u64,
    clients_hwm: usize,
}

#[derive(Debug, Clone)]
struct Session {
    id: u16,
    room_key: String,
    game_client_id: i16,
    voice_team: i16,
    group_id: u16,
    last_seen: Instant,
    rate_window_started: Instant,
    rate_window_packets: u32,
    rate_window_bytes: u32,
}

impl Session {
    fn new(now: Instant, id: u16, room_key: String, game_client_id: i16, voice_team: i16) -> Self {
        Self {
            id,
            room_key,
            game_client_id,
            voice_team,
            group_id: 0,
            last_seen: now,
            rate_window_started: now,
            rate_window_packets: 0,
            rate_window_bytes: 0,
        }
    }

    fn allow_packet(&mut self, now: Instant, max_pps: u32, max_bps: u32, bytes: usize) -> bool {
        if now.duration_since(self.rate_window_started) >= Duration::from_secs(1) {
            self.rate_window_started = now;
            self.rate_window_packets = 0;
            self.rate_window_bytes = 0;
        }
        if self.rate_window_packets >= max_pps {
            return false;
        }
        if self.rate_window_bytes.saturating_add(bytes as u32) > max_bps {
            return false;
        }
        self.rate_window_packets += 1;
        self.rate_window_bytes += bytes as u32;
        true
    }
}

fn normalize_voice_team(team: i16) -> i16 {
    if team <= 0 {
        0
    } else {
        team
    }
}

#[derive(Debug, Clone)]
struct VoiceGroup {
    id: u16,
    name: String,
    name_key: String,
    privacy: u8,
    owner_game_client_id: i16,
    created_at: Instant,
    last_active: Instant,
}

#[derive(Debug)]
struct RoomState {
    next_group_id: u16,
    groups: HashMap<u16, VoiceGroup>,
    name_to_id: HashMap<String, u16>,
    invites: HashMap<u16, HashMap<i16, Instant>>,
    last_seen: Instant,
}

impl RoomState {
    fn new(now: Instant) -> Self {
        let mut groups = HashMap::new();
        let mut name_to_id = HashMap::new();
        let team0 = VoiceGroup {
            id: 0,
            name: "team0".to_string(),
            name_key: "team0".to_string(),
            privacy: GROUP_PRIVACY_PUBLIC,
            owner_game_client_id: -1,
            created_at: now,
            last_active: now,
        };
        groups.insert(0, team0);
        name_to_id.insert("team0".to_string(), 0);
        Self {
            next_group_id: 1,
            groups,
            name_to_id,
            invites: HashMap::new(),
            last_seen: now,
        }
    }
}

fn read_u8(buf: &[u8], offset: &mut usize) -> Option<u8> {
    if *offset + 1 > buf.len() {
        return None;
    }
    let v = buf[*offset];
    *offset += 1;
    Some(v)
}

fn read_u16_be(buf: &[u8], offset: &mut usize) -> Option<u16> {
    if *offset + 2 > buf.len() {
        return None;
    }
    let v = u16::from_be_bytes([buf[*offset], buf[*offset + 1]]);
    *offset += 2;
    Some(v)
}

fn read_i16_be(buf: &[u8], offset: &mut usize) -> Option<i16> {
    read_u16_be(buf, offset).map(|v| v as i16)
}

fn read_u32_be(buf: &[u8], offset: &mut usize) -> Option<u32> {
    if *offset + 4 > buf.len() {
        return None;
    }
    let v = u32::from_be_bytes([
        buf[*offset],
        buf[*offset + 1],
        buf[*offset + 2],
        buf[*offset + 3],
    ]);
    *offset += 4;
    Some(v)
}

fn read_i32_be(buf: &[u8], offset: &mut usize) -> Option<i32> {
    read_u32_be(buf, offset).map(|v| v as i32)
}

fn write_u8(out: &mut Vec<u8>, v: u8) {
    out.push(v);
}

fn write_u16_be(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn write_i16_be(out: &mut Vec<u8>, v: i16) {
    write_u16_be(out, v as u16);
}

fn write_u32_be(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn write_i32_be(out: &mut Vec<u8>, v: i32) {
    write_u32_be(out, v as u32);
}

fn read_string(buf: &[u8], offset: &mut usize, max_len: usize) -> Option<String> {
    let len = read_u16_be(buf, offset)? as usize;
    if len > max_len {
        return None;
    }
    if *offset + len > buf.len() {
        return None;
    }
    let s = String::from_utf8_lossy(&buf[*offset..*offset + len]).to_string();
    *offset += len;
    Some(s)
}

fn write_string(out: &mut Vec<u8>, s: &str, max_len: usize) {
    let bytes = s.as_bytes();
    let len = bytes.len().min(max_len);
    write_u16_be(out, len as u16);
    out.extend_from_slice(&bytes[..len]);
}

fn norm_name_key(s: &str) -> String {
    s.trim().to_lowercase()
}

fn write_header(out: &mut Vec<u8>, ty: u8) {
    write_u32_be(out, PROTOCOL_MAGIC);
    write_u8(out, ty);
    write_u8(out, PROTOCOL_VERSION);
}

fn parse_header(buf: &[u8]) -> Option<(u8, usize)> {
    let mut offset = 0usize;
    let magic = read_u32_be(buf, &mut offset)?;
    let ty = read_u8(buf, &mut offset)?;
    let version = read_u8(buf, &mut offset)?;
    if magic != PROTOCOL_MAGIC || version != PROTOCOL_VERSION {
        return None;
    }
    Some((ty, offset))
}

fn print_usage() {
    println!("BestClient Voice Server (BVC1/UDP)");
    println!("Usage: bestclient-voice-server [options]");
    println!("  --bind <ip:port>              UDP bind address (default: {DEFAULT_BIND})");
    println!("  --timeout-secs <n>            Client timeout in seconds (default: 30)");
    println!("  --peerlist-interval-secs <n>  Peer list broadcast interval in seconds (default: 5)");
    println!("  --max-pps <n>                 Per-client packets/sec limit (default: 120)");
    println!("  --max-bps <n>                 Per-client bytes/sec limit (default: 75000)");
    println!("  --max-clients <n>             Max tracked clients (default: 4096)");
    println!("  --quiet                       Disable periodic stats log");
    println!("  --help                        Show this help");
}

fn parse_args() -> Result<ParseResult, String> {
    let mut cfg = Config {
        bind: DEFAULT_BIND.to_string(),
        timeout: Duration::from_secs(30),
        peer_list_interval: Duration::from_secs(5),
        max_pps: 120,
        max_bps: 75_000,
        max_clients: 4096,
        quiet: false,
    };

    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--bind" => {
                cfg.bind = args.next().ok_or_else(|| "--bind requires value".to_string())?;
            }
            "--timeout-secs" => {
                let value = args
                    .next()
                    .ok_or_else(|| "--timeout-secs requires value".to_string())?;
                let secs: u64 = value.parse().map_err(|_| "invalid --timeout-secs".to_string())?;
                if secs == 0 {
                    return Err("--timeout-secs must be > 0".to_string());
                }
                cfg.timeout = Duration::from_secs(secs);
            }
            "--peerlist-interval-secs" => {
                let value = args.next().ok_or_else(|| "--peerlist-interval-secs requires value".to_string())?;
                let secs: u64 = value
                    .parse()
                    .map_err(|_| "invalid --peerlist-interval-secs".to_string())?;
                if secs == 0 {
                    return Err("--peerlist-interval-secs must be > 0".to_string());
                }
                cfg.peer_list_interval = Duration::from_secs(secs);
            }
            "--max-pps" => {
                let value = args.next().ok_or_else(|| "--max-pps requires value".to_string())?;
                let pps: u32 = value.parse().map_err(|_| "invalid --max-pps".to_string())?;
                if pps == 0 {
                    return Err("--max-pps must be > 0".to_string());
                }
                cfg.max_pps = pps;
            }
            "--max-bps" => {
                let value = args.next().ok_or_else(|| "--max-bps requires value".to_string())?;
                let bps: u32 = value.parse().map_err(|_| "invalid --max-bps".to_string())?;
                if bps == 0 {
                    return Err("--max-bps must be > 0".to_string());
                }
                cfg.max_bps = bps;
            }
            "--max-clients" => {
                let value = args.next().ok_or_else(|| "--max-clients requires value".to_string())?;
                let clients: usize = value.parse().map_err(|_| "invalid --max-clients".to_string())?;
                if clients == 0 {
                    return Err("--max-clients must be > 0".to_string());
                }
                cfg.max_clients = clients;
            }
            "--quiet" => cfg.quiet = true,
            "--help" | "-h" => return Ok(ParseResult::Help),
            _ => return Err(format!("unknown argument: {arg}")),
        }
    }

    Ok(ParseResult::Config(cfg))
}

fn build_hello_ack(id: u16, server_flags: u16) -> Vec<u8> {
    let mut out = Vec::with_capacity(10);
    write_header(&mut out, PACKET_HELLO_ACK);
    write_u16_be(&mut out, id);
    write_u16_be(&mut out, server_flags);
    out
}

fn build_pong(token: u16) -> Vec<u8> {
    let mut out = Vec::with_capacity(8);
    write_header(&mut out, PACKET_PONG);
    write_u16_be(&mut out, token);
    out
}

fn build_group_list(entries: &[(u16, u8, u16, String)]) -> Vec<u8> {
    // (id, privacy, members_count, name)
    let mut out = Vec::with_capacity(16 + entries.len() * 16);
    write_header(&mut out, PACKET_GROUP_LIST);
    write_u16_be(&mut out, entries.len() as u16);
    for (id, privacy, members, name) in entries {
        write_u16_be(&mut out, *id);
        write_u8(&mut out, *privacy);
        write_u16_be(&mut out, *members);
        write_string(&mut out, name, GROUP_NAME_MAX);
    }
    out
}

fn build_group_create_ack(status: u8, group_id: u16, privacy: u8, name: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(24 + name.len());
    write_header(&mut out, PACKET_GROUP_CREATE_ACK);
    write_u8(&mut out, status);
    write_u16_be(&mut out, group_id);
    write_u8(&mut out, privacy);
    write_string(&mut out, name, GROUP_NAME_MAX);
    out
}

fn build_group_join_ack(status: u8, group_id: u16, privacy: u8, name: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(24 + name.len());
    write_header(&mut out, PACKET_GROUP_JOIN_ACK);
    write_u8(&mut out, status);
    write_u16_be(&mut out, group_id);
    write_u8(&mut out, privacy);
    write_string(&mut out, name, GROUP_NAME_MAX);
    out
}

fn build_group_set_privacy_ack(status: u8, group_id: u16, privacy: u8) -> Vec<u8> {
    let mut out = Vec::with_capacity(16);
    write_header(&mut out, PACKET_GROUP_SET_PRIVACY_ACK);
    write_u8(&mut out, status);
    write_u16_be(&mut out, group_id);
    write_u8(&mut out, privacy);
    out
}

fn build_group_invite_evt(group_id: u16, privacy: u8, name: &str, inviter_game_client_id: i16) -> Vec<u8> {
    let mut out = Vec::with_capacity(24 + name.len());
    write_header(&mut out, PACKET_GROUP_INVITE_EVT);
    write_u16_be(&mut out, group_id);
    write_u8(&mut out, privacy);
    write_string(&mut out, name, GROUP_NAME_MAX);
    write_i16_be(&mut out, inviter_game_client_id);
    out
}

fn build_peer_list(room_sessions: &[(SocketAddr, &Session)]) -> Vec<u8> {
    let mut out = Vec::with_capacity(8 + 2 + room_sessions.len() * 2);
    write_header(&mut out, PACKET_PEER_LIST);
    write_u16_be(&mut out, room_sessions.len() as u16);
    for (_, s) in room_sessions {
        write_u16_be(&mut out, s.id);
    }
    out
}

fn build_peer_list_ex(room_sessions: &[(SocketAddr, &Session)]) -> Vec<u8> {
    let mut out = Vec::with_capacity(8 + 2 + room_sessions.len() * 4);
    write_header(&mut out, PACKET_PEER_LIST_EX);
    write_u16_be(&mut out, room_sessions.len() as u16);
    for (_, s) in room_sessions {
        write_u16_be(&mut out, s.id);
        write_i16_be(&mut out, s.game_client_id);
    }
    out
}

fn build_voice_relay(sender_id: u16, voice_payload: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(8 + 2 + voice_payload.len());
    write_header(&mut out, PACKET_VOICE_RELAY);
    write_u16_be(&mut out, sender_id);
    out.extend_from_slice(voice_payload);
    out
}

fn run_server(cfg: Config) -> io::Result<()> {
    let socket = UdpSocket::bind(&cfg.bind)?;
    socket.set_read_timeout(Some(Duration::from_millis(250)))?;

    let mut sessions: HashMap<SocketAddr, Session> = HashMap::new();
    let mut identity_to_id: HashMap<(String, i16), u16> = HashMap::new();
    let mut next_id: u16 = 1;
    let mut room_states: HashMap<String, RoomState> = HashMap::new();

    let mut stats = Stats::default();
    let mut recv_buf = [0u8; MAX_PACKET_SIZE];
    let mut last_cleanup = Instant::now();
    let mut last_peerlist = Instant::now();
    let mut last_stats_print = Instant::now();

    println!(
        "[voice] listening on {} (timeout={}s, peerlist={}s, max_pps={}, max_bps={}, max_clients={})",
        cfg.bind,
        cfg.timeout.as_secs(),
        cfg.peer_list_interval.as_secs(),
        cfg.max_pps,
        cfg.max_bps,
        cfg.max_clients
    );

    loop {
        match socket.recv_from(&mut recv_buf) {
            Ok((len, addr)) => {
                let now = Instant::now();
                stats.packets_in += 1;

                if len < 6 || len > MAX_PACKET_SIZE {
                    stats.packets_drop_parse += 1;
                    continue;
                }

                let packet = &recv_buf[..len];
                let (ty, mut offset) = match parse_header(packet) {
                    Some(v) => v,
                    None => {
                        stats.packets_drop_parse += 1;
                        continue;
                    }
                };

                if ty == PACKET_PING {
                    let token = match read_u16_be(packet, &mut offset) {
                        Some(v) => v,
                        None => {
                            stats.packets_drop_parse += 1;
                            continue;
                        }
                    };
                    let pong = build_pong(token);
                    let _ = socket.send_to(&pong, addr);
                    stats.packets_out += 1;
                    stats.ping_requests += 1;
                    continue;
                }

                if !sessions.contains_key(&addr) && sessions.len() >= cfg.max_clients {
                    stats.packets_drop_capacity += 1;
                    continue;
                }

                if ty == PACKET_HELLO {
                    stats.hello_packets += 1;
                    let _client_version = match read_u16_be(packet, &mut offset) {
                        Some(v) => v,
                        None => {
                            stats.packets_drop_parse += 1;
                            continue;
                        }
                    };

                    let mut room_key = String::new();
                    let mut game_client_id: i16 = -1;
                    let mut voice_team: i16 = 0;
                    if offset < packet.len() {
                        let room_size = match read_u16_be(packet, &mut offset) {
                            Some(v) => v as usize,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        if room_size > ROOM_KEY_MAX || offset + room_size > packet.len() {
                            stats.packets_drop_parse += 1;
                            continue;
                        }
                        room_key = String::from_utf8_lossy(&packet[offset..offset + room_size]).to_string();
                        offset += room_size;
                        if offset < packet.len() {
                            game_client_id = match read_i16_be(packet, &mut offset) {
                                Some(v) => v,
                                None => {
                                    stats.packets_drop_parse += 1;
                                    continue;
                                }
                            };
                        }
                        if offset < packet.len() {
                            voice_team = match read_i16_be(packet, &mut offset) {
                                Some(v) => normalize_voice_team(v),
                                None => {
                                    stats.packets_drop_parse += 1;
                                    continue;
                                }
                            };
                        }
                    }

                    let mut id_changed = false;
                    let mut room_changed = false;
                    let mut game_changed = false;
                    let mut team_changed = false;

                    // Ensure room state exists (includes mandatory team0).
                    let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                    rs.last_seen = now;

                    if let Some(existing) = sessions.get_mut(&addr) {
                        room_changed = existing.room_key != room_key;
                        game_changed = existing.game_client_id != game_client_id;
                        team_changed = existing.voice_team != voice_team;
                        existing.last_seen = now;
                        existing.room_key = room_key.clone();
                        existing.game_client_id = game_client_id;
                        existing.voice_team = voice_team;
                        if room_changed {
                            existing.group_id = 0;
                        }
                    } else {
                        let mut reuse_id: Option<u16> = None;
                        if game_client_id != -1 {
                            if let Some(id) = identity_to_id.get(&(room_key.clone(), game_client_id)).copied() {
                                // Only reuse if not currently active.
                                if !sessions.values().any(|s| s.id == id) {
                                    reuse_id = Some(id);
                                }
                            }
                        }

                        let id = if let Some(id) = reuse_id {
                            id
                        } else {
                            let mut id = next_id;
                            next_id = next_id.wrapping_add(1);
                            if next_id == 0 {
                                next_id = 1;
                            }
                            // avoid collision with active sessions
                            while sessions.values().any(|s| s.id == id) {
                                id = next_id;
                                next_id = next_id.wrapping_add(1);
                                if next_id == 0 {
                                    next_id = 1;
                                }
                            }
                            id
                        };

                        sessions.insert(addr, Session::new(now, id, room_key.clone(), game_client_id, voice_team));
                        id_changed = true;
                    }

                    if game_client_id != -1 {
                        identity_to_id.insert((room_key.clone(), game_client_id), sessions[&addr].id);
                    }

                    if sessions.len() > stats.clients_hwm {
                        stats.clients_hwm = sessions.len();
                    }

                    let ack = build_hello_ack(sessions[&addr].id, 0);
                    let _ = socket.send_to(&ack, addr);
                    stats.packets_out += 1;

                    if id_changed || room_changed || game_changed || team_changed {
                        // Force immediate peer list refresh (room membership changed).
                        last_peerlist = Instant::now() - cfg.peer_list_interval;
                    }
                    continue;
                }

                if ty == PACKET_GOODBYE {
                    if let Some(removed) = sessions.remove(&addr) {
                        if removed.game_client_id != -1 {
                            let key = (removed.room_key.clone(), removed.game_client_id);
                            if identity_to_id.get(&key).copied() == Some(removed.id) {
                                identity_to_id.remove(&key);
                            }
                        }
                        last_peerlist = Instant::now() - cfg.peer_list_interval;
                    }
                    continue;
                }

                // Only rate-limit packets from known sessions.
                let allow = sessions
                    .get_mut(&addr)
                    .map(|s| {
                        s.last_seen = now;
                        s.allow_packet(now, cfg.max_pps, cfg.max_bps, len)
                    })
                    .unwrap_or(false);
                if !allow {
                    stats.packets_drop_rate += 1;
                    continue;
                }

                match ty {
                    PACKET_VOICE => {
                        stats.voice_packets += 1;

                        let sender = match sessions.get(&addr) {
                            Some(s) => s.clone(),
                            None => continue,
                        };

                        // Voice payload starts after header; relay payload should match:
                        // team(i16) posx(i32) posy(i32) seq(u16) size(u16) data
                        // Validate basic structure without decoding.
                        let voice_payload = &packet[offset..];
                        if voice_payload.len() < 2 + 4 + 4 + 2 + 2 {
                            stats.packets_drop_parse += 1;
                            continue;
                        }
                        let mut check_off = 0usize;
                        let _team = read_i16_be(voice_payload, &mut check_off).unwrap();
                        let _posx = read_i32_be(voice_payload, &mut check_off).unwrap();
                        let _posy = read_i32_be(voice_payload, &mut check_off).unwrap();
                        let _seq = read_u16_be(voice_payload, &mut check_off).unwrap();
                        let opus_size = match read_u16_be(voice_payload, &mut check_off) {
                            Some(v) => v as usize,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        if opus_size == 0 || opus_size > 400 || check_off + opus_size > voice_payload.len() {
                            stats.packets_drop_parse += 1;
                            continue;
                        }

                        let relay = build_voice_relay(sender.id, voice_payload);
                        let sender_room = sender.room_key;
                        let sender_team = sender.voice_team;
                        for (peer_addr, peer_sess) in sessions.iter() {
                            if *peer_addr == addr {
                                continue;
                            }
                            if peer_sess.room_key != sender_room {
                                continue;
                            }
                            if peer_sess.voice_team != sender_team {
                                continue;
                            }
                            if socket.send_to(&relay, peer_addr).is_ok() {
                                stats.packets_out += 1;
                            }
                        }
                    }
                    PACKET_GROUP_LIST_REQ => {
                        let sess = match sessions.get(&addr) {
                            Some(s) => s,
                            None => continue,
                        };
                        let room_key = sess.room_key.clone();
                        let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                        rs.last_seen = now;

                        let mut ids: Vec<u16> = rs.groups.keys().copied().collect();
                        ids.sort_unstable();
                        let mut entries: Vec<(u16, u8, u16, String)> = Vec::with_capacity(ids.len());
                        for gid in ids {
                            let members = sessions
                                .values()
                                .filter(|s| s.room_key == room_key && s.group_id == gid)
                                .count();
                            if let Some(g) = rs.groups.get_mut(&gid) {
                                if members > 0 {
                                    g.last_active = now;
                                }
                                entries.push((gid, g.privacy, members.min(u16::MAX as usize) as u16, g.name.clone()));
                            }
                        }
                        let out = build_group_list(&entries);
                        if socket.send_to(&out, addr).is_ok() {
                            stats.packets_out += 1;
                        }
                    }
                    PACKET_GROUP_CREATE_REQ => {
                        let privacy = match read_u8(packet, &mut offset) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        let name = match read_string(packet, &mut offset, GROUP_NAME_MAX) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };

                        let sess = match sessions.get(&addr) {
                            Some(s) => s,
                            None => continue,
                        };
                        let room_key = sess.room_key.clone();
                        let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                        rs.last_seen = now;

                        let name_key = norm_name_key(&name);
                        let valid_privacy = privacy == GROUP_PRIVACY_PUBLIC || privacy == GROUP_PRIVACY_PRIVATE;
                        let can_create = sess.game_client_id != -1;
                        if !valid_privacy || name_key.is_empty() || name_key == "team0" || !can_create {
                            let out = build_group_create_ack(GROUP_STATUS_INVALID, 0, GROUP_PRIVACY_PRIVATE, &name);
                            if socket.send_to(&out, addr).is_ok() {
                                stats.packets_out += 1;
                            }
                            continue;
                        }

                        if rs.name_to_id.contains_key(&name_key) {
                            let out = build_group_create_ack(GROUP_STATUS_EXISTS, 0, privacy, &name);
                            if socket.send_to(&out, addr).is_ok() {
                                stats.packets_out += 1;
                            }
                            continue;
                        }

                        let mut gid = rs.next_group_id;
                        rs.next_group_id = rs.next_group_id.wrapping_add(1);
                        if rs.next_group_id == 0 {
                            rs.next_group_id = 1;
                        }
                        while rs.groups.contains_key(&gid) || gid == 0 {
                            gid = rs.next_group_id;
                            rs.next_group_id = rs.next_group_id.wrapping_add(1);
                            if rs.next_group_id == 0 {
                                rs.next_group_id = 1;
                            }
                        }

                        let g = VoiceGroup {
                            id: gid,
                            name: name.trim().to_string(),
                            name_key: name_key.clone(),
                            privacy,
                            owner_game_client_id: sess.game_client_id,
                            created_at: now,
                            last_active: now,
                        };
                        rs.groups.insert(gid, g);
                        rs.name_to_id.insert(name_key, gid);

                        let out = build_group_create_ack(GROUP_STATUS_OK, gid, privacy, name.trim());
                        if socket.send_to(&out, addr).is_ok() {
                            stats.packets_out += 1;
                        }
                    }
                    PACKET_GROUP_JOIN_ID_REQ | PACKET_GROUP_JOIN_NAME_REQ => {
                        let (target_gid, requested_name) = if ty == PACKET_GROUP_JOIN_ID_REQ {
                            let gid = match read_u16_be(packet, &mut offset) {
                                Some(v) => v,
                                None => {
                                    stats.packets_drop_parse += 1;
                                    continue;
                                }
                            };
                            (gid, String::new())
                        } else {
                            let name = match read_string(packet, &mut offset, GROUP_NAME_MAX) {
                                Some(v) => v,
                                None => {
                                    stats.packets_drop_parse += 1;
                                    continue;
                                }
                            };
                            (u16::MAX, name)
                        };

                        let sess_snapshot = match sessions.get(&addr) {
                            Some(s) => s.clone(),
                            None => continue,
                        };
                        let room_key = sess_snapshot.room_key.clone();
                        let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                        rs.last_seen = now;

                        let gid = if ty == PACKET_GROUP_JOIN_ID_REQ {
                            target_gid
                        } else {
                            let key = norm_name_key(&requested_name);
                            match rs.name_to_id.get(&key).copied() {
                                Some(id) => id,
                                None => {
                                    let out = build_group_join_ack(GROUP_STATUS_NOT_FOUND, 0, GROUP_PRIVACY_PRIVATE, requested_name.trim());
                                    if socket.send_to(&out, addr).is_ok() {
                                        stats.packets_out += 1;
                                    }
                                    continue;
                                }
                            }
                        };

                        let (status, privacy, name) = if gid == 0 {
                            (GROUP_STATUS_OK, GROUP_PRIVACY_PUBLIC, "team0".to_string())
                        } else if let Some(g) = rs.groups.get(&gid) {
                            // Access control for private groups.
                            if g.privacy == GROUP_PRIVACY_PUBLIC {
                                (GROUP_STATUS_OK, g.privacy, g.name.clone())
                            } else if sess_snapshot.game_client_id != -1 && sess_snapshot.game_client_id == g.owner_game_client_id {
                                (GROUP_STATUS_OK, g.privacy, g.name.clone())
                            } else if sess_snapshot.game_client_id != -1 {
                                let invited = rs
                                    .invites
                                    .get_mut(&gid)
                                    .and_then(|m| m.get(&sess_snapshot.game_client_id).copied());
                                if let Some(ts) = invited {
                                    if now.duration_since(ts) <= INVITE_TTL {
                                        if let Some(m) = rs.invites.get_mut(&gid) {
                                            m.remove(&sess_snapshot.game_client_id);
                                        }
                                        (GROUP_STATUS_OK, g.privacy, g.name.clone())
                                    } else {
                                        if let Some(m) = rs.invites.get_mut(&gid) {
                                            m.remove(&sess_snapshot.game_client_id);
                                        }
                                        (GROUP_STATUS_NOT_INVITED, g.privacy, g.name.clone())
                                    }
                                } else {
                                    (GROUP_STATUS_NOT_INVITED, g.privacy, g.name.clone())
                                }
                            } else {
                                (GROUP_STATUS_NOT_INVITED, g.privacy, g.name.clone())
                            }
                        } else {
                            (GROUP_STATUS_NOT_FOUND, GROUP_PRIVACY_PRIVATE, requested_name.trim().to_string())
                        };

                        if status == GROUP_STATUS_OK {
                            if let Some(s) = sessions.get_mut(&addr) {
                                s.group_id = gid;
                                s.last_seen = now;
                            }
                            last_peerlist = Instant::now() - cfg.peer_list_interval;
                        }
                        let out = build_group_join_ack(status, gid, privacy, &name);
                        if socket.send_to(&out, addr).is_ok() {
                            stats.packets_out += 1;
                        }
                    }
                    PACKET_GROUP_SET_PRIVACY_REQ => {
                        let gid = match read_u16_be(packet, &mut offset) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        let privacy = match read_u8(packet, &mut offset) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        if privacy != GROUP_PRIVACY_PUBLIC && privacy != GROUP_PRIVACY_PRIVATE {
                            let out = build_group_set_privacy_ack(GROUP_STATUS_INVALID, gid, privacy);
                            if socket.send_to(&out, addr).is_ok() {
                                stats.packets_out += 1;
                            }
                            continue;
                        }

                        let sess = match sessions.get(&addr) {
                            Some(s) => s.clone(),
                            None => continue,
                        };
                        let room_key = sess.room_key.clone();
                        let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                        rs.last_seen = now;

                        let status = if gid == 0 {
                            GROUP_STATUS_FORBIDDEN
                        } else if let Some(g) = rs.groups.get_mut(&gid) {
                            if sess.game_client_id != -1 && sess.game_client_id == g.owner_game_client_id {
                                g.privacy = privacy;
                                GROUP_STATUS_OK
                            } else {
                                GROUP_STATUS_FORBIDDEN
                            }
                        } else {
                            GROUP_STATUS_NOT_FOUND
                        };
                        let out = build_group_set_privacy_ack(status, gid, privacy);
                        if socket.send_to(&out, addr).is_ok() {
                            stats.packets_out += 1;
                        }
                    }
                    PACKET_GROUP_INVITE_REQ => {
                        let gid = match read_u16_be(packet, &mut offset) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };
                        let target = match read_i16_be(packet, &mut offset) {
                            Some(v) => v,
                            None => {
                                stats.packets_drop_parse += 1;
                                continue;
                            }
                        };

                        let sender = match sessions.get(&addr) {
                            Some(s) => s.clone(),
                            None => continue,
                        };
                        if gid == 0 || target == -1 || sender.game_client_id == -1 {
                            continue;
                        }
                        if sender.group_id != gid {
                            continue;
                        }

                        let room_key = sender.room_key.clone();
                        let rs = room_states.entry(room_key.clone()).or_insert_with(|| RoomState::new(now));
                        rs.last_seen = now;

                        let (privacy, name) = match rs.groups.get(&gid) {
                            Some(g) => (g.privacy, g.name.clone()),
                            None => continue,
                        };

                        rs.invites.entry(gid).or_default().insert(target, now);
                        let evt = build_group_invite_evt(gid, privacy, &name, sender.game_client_id);
                        for (peer_addr, peer_sess) in sessions.iter() {
                            if peer_sess.room_key != room_key {
                                continue;
                            }
                            if peer_sess.game_client_id != target {
                                continue;
                            }
                            if socket.send_to(&evt, peer_addr).is_ok() {
                                stats.packets_out += 1;
                            }
                        }
                    }
                    _ => {
                        continue;
                    }
                }

                if !cfg.quiet && now.duration_since(last_stats_print) >= Duration::from_secs(5) {
                    println!(
                        "[voice] clients={} in={} out={} hello={} voice={} ping={} drop(parse/rate/cap)={}/{}/{}",
                        sessions.len(),
                        stats.packets_in,
                        stats.packets_out,
                        stats.hello_packets,
                        stats.voice_packets,
                        stats.ping_requests,
                        stats.packets_drop_parse,
                        stats.packets_drop_rate,
                        stats.packets_drop_capacity,
                    );
                    last_stats_print = now;
                }
            }
            Err(err) if err.kind() == io::ErrorKind::WouldBlock || err.kind() == io::ErrorKind::TimedOut => {
                // periodic tasks only
            }
            Err(err) => return Err(err),
        }

        let now = Instant::now();

        if now.duration_since(last_peerlist) >= cfg.peer_list_interval {
            // Group sessions by (room_key, voice_team) to isolate audio/peer lists.
            let mut groups: HashMap<(&str, i16), Vec<(SocketAddr, &Session)>> = HashMap::new();
            for (addr, sess) in sessions.iter() {
                groups
                    .entry((sess.room_key.as_str(), sess.voice_team))
                    .or_default()
                    .push((*addr, sess));
            }

            for ((_room, _gid), group_sessions) in groups.iter() {
                if group_sessions.len() > (u16::MAX as usize) {
                    continue;
                }
                let peer_list = build_peer_list(group_sessions);
                let peer_list_ex = build_peer_list_ex(group_sessions);
                for (addr, _) in group_sessions.iter() {
                    let _ = socket.send_to(&peer_list, addr);
                    let _ = socket.send_to(&peer_list_ex, addr);
                    stats.packets_out += 2;
                }
            }
            last_peerlist = now;
        }

        if now.duration_since(last_cleanup) >= Duration::from_secs(1) {
            let timeout = cfg.timeout;
            let mut removed_any = false;
            sessions.retain(|_addr, sess| {
                let keep = now.duration_since(sess.last_seen) <= timeout;
                if !keep {
                    removed_any = true;
                }
                keep
            });

            // Cleanup group state (invites + stale empty groups).
            let mut rooms_with_sessions: HashSet<&str> = HashSet::new();
            for s in sessions.values() {
                rooms_with_sessions.insert(s.room_key.as_str());
            }
            room_states.retain(|room_key, rs| {
                // Prune expired invites.
                for inv in rs.invites.values_mut() {
                    inv.retain(|_gid, ts| now.duration_since(*ts) <= INVITE_TTL);
                }
                rs.invites.retain(|_gid, inv| !inv.is_empty());

                // Count members per group for this room.
                let mut member_counts: HashMap<u16, usize> = HashMap::new();
                for s in sessions.values() {
                    if s.room_key == *room_key {
                        *member_counts.entry(s.group_id).or_insert(0) += 1;
                    }
                }

                // Update last_active for non-empty groups, and remove stale empty ones (except team0).
                let mut to_remove: Vec<u16> = Vec::new();
                for (gid, g) in rs.groups.iter_mut() {
                    if *gid == 0 {
                        continue;
                    }
                    let members = member_counts.get(gid).copied().unwrap_or(0);
                    if members > 0 {
                        g.last_active = now;
                    } else if now.duration_since(g.last_active) > GROUP_EMPTY_TTL {
                        to_remove.push(*gid);
                    }
                }
                for gid in to_remove {
                    if let Some(g) = rs.groups.remove(&gid) {
                        rs.name_to_id.remove(&g.name_key);
                    }
                    rs.invites.remove(&gid);
                }

                // Drop room state when it's unused and only contains team0.
                let keep_room = rooms_with_sessions.contains(room_key.as_str())
                    || rs.groups.len() > 1
                    || now.duration_since(rs.last_seen) <= GROUP_EMPTY_TTL;
                keep_room
            });
            if removed_any {
                // clean identity map best-effort
                identity_to_id.retain(|(room, gid), id| {
                    if *gid == -1 {
                        return false;
                    }
                    sessions.values().any(|s| &s.room_key == room && s.game_client_id == *gid && s.id == *id)
                });
                last_peerlist = Instant::now() - cfg.peer_list_interval;
            }
            last_cleanup = now;
        }
    }
}

fn main() {
    let cfg = match parse_args() {
        Ok(ParseResult::Help) => {
            print_usage();
            return;
        }
        Ok(ParseResult::Config(cfg)) => cfg,
        Err(err) => {
            eprintln!("Error: {err}");
            eprintln!();
            print_usage();
            process::exit(2);
        }
    };

    if let Err(err) = run_server(cfg) {
        eprintln!("Fatal: {err}");
        process::exit(1);
    }
}
