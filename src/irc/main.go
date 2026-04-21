// Copyright 2026 BestProject Team
package main

import (
	"bufio"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	_ "github.com/mattn/go-sqlite3"
	"golang.org/x/crypto/argon2"
)

const (
	defaultListen = ":6667"
	protocolName  = "bestgram"
	maxLineBytes  = 32 * 1024
	maxMsgLen     = 1200
	maxHistory    = 100
)

var generalChannels = []string{"international", "english", "russian", "chinese", "french"}

type config struct {
	dir      string
	listen   string
	seed     []byte
	certPath string
	keyPath  string
	dbPath   string
}

type server struct {
	cfg config
	db  *sql.DB
	sec security

	mu          sync.Mutex
	clients     map[*client]struct{}
	userClients map[int64]map[*client]struct{}
	channels    map[string]map[*client]struct{}
	limiters    map[string]*tokenBucket
	failures    map[string]*loginFailure
}

type client struct {
	srv       *server
	conn      net.Conn
	addr      string
	userID    int64
	login     string
	display   string
	status    string
	invisible bool

	mu       sync.Mutex
	channels map[string]bool
	send     chan envelope
	closed   chan struct{}
}

type envelope map[string]any

type incoming struct {
	Type        string `json:"type"`
	RequestID   string `json:"request_id,omitempty"`
	Login       string `json:"login,omitempty"`
	NewLogin    string `json:"new_login,omitempty"`
	Password    string `json:"password,omitempty"`
	NewPassword string `json:"new_password,omitempty"`
	Token       string `json:"token,omitempty"`
	Channel     string `json:"channel,omitempty"`
	TargetType  string `json:"target_type,omitempty"`
	Target      string `json:"target,omitempty"`
	Text        string `json:"text,omitempty"`
	Query       string `json:"query,omitempty"`
	Status      string `json:"status,omitempty"`
	Description string `json:"description,omitempty"`
	BannerColor string `json:"banner_color,omitempty"`
	Skin        string `json:"skin,omitempty"`
	UserID      int64  `json:"user_id,omitempty"`
	BeforeID    int64  `json:"before_id,omitempty"`
	Limit       int    `json:"limit,omitempty"`
}

type security struct {
	seed []byte
	aead cipher.AEAD
}

type tokenBucket struct {
	tokens float64
	last   time.Time
}

type loginFailure struct {
	count   int
	until   time.Time
	lastHit time.Time
}

func main() {
	dirFlag := flag.String("dir", "", "runtime directory (default ~/irc-bc)")
	flag.Parse()

	cfg, err := loadConfig(*dirFlag)
	if err != nil {
		log.Fatal(err)
	}
	tlsCfg, fingerprint, err := tlsConfig(cfg)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("BestGram TLS fingerprint sha256=%s", fingerprint)

	srv, err := newServer(cfg)
	if err != nil {
		log.Fatal(err)
	}
	defer srv.db.Close()

	ln, err := tls.Listen("tcp", cfg.listen, tlsCfg)
	if err != nil {
		log.Fatal(err)
	}
	defer ln.Close()
	log.Printf("BestGram listening on %s, data=%s", cfg.listen, cfg.dir)

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}
		go srv.handleConn(conn)
	}
}

func loadConfig(dirFlag string) (config, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return config{}, err
	}
	dir := dirFlag
	if dir == "" {
		dir = filepath.Join(home, "irc-bc")
	}
	if err := os.MkdirAll(dir, 0700); err != nil {
		return config{}, err
	}
	envPath := filepath.Join(dir, ".env")
	values := readEnv(envPath)
	if values["BC_IRC_LISTEN"] == "" {
		values["BC_IRC_LISTEN"] = defaultListen
	}
	if values["BC_IRC_CERT"] == "" {
		values["BC_IRC_CERT"] = filepath.Join(dir, "server.crt")
	}
	if values["BC_IRC_KEY"] == "" {
		values["BC_IRC_KEY"] = filepath.Join(dir, "server.key")
	}
	if values["BC_IRC_SEED"] == "" {
		seed := make([]byte, 32)
		if _, err := rand.Read(seed); err != nil {
			return config{}, err
		}
		values["BC_IRC_SEED"] = base64.RawStdEncoding.EncodeToString(seed)
		if err := writeEnv(envPath, values); err != nil {
			return config{}, err
		}
	}
	seed, err := base64.RawStdEncoding.DecodeString(values["BC_IRC_SEED"])
	if err != nil || len(seed) < 32 {
		return config{}, errors.New("BC_IRC_SEED must be base64 raw std encoding of at least 32 bytes")
	}
	return config{
		dir:      dir,
		listen:   values["BC_IRC_LISTEN"],
		seed:     seed,
		certPath: values["BC_IRC_CERT"],
		keyPath:  values["BC_IRC_KEY"],
		dbPath:   filepath.Join(dir, "irc.sqlite3"),
	}, nil
}

func readEnv(path string) map[string]string {
	values := map[string]string{}
	data, err := os.ReadFile(path)
	if err != nil {
		return values
	}
	scanner := bufio.NewScanner(strings.NewReader(string(data)))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if ok {
			values[strings.TrimSpace(k)] = strings.Trim(strings.TrimSpace(v), `"`)
		}
	}
	return values
}

func writeEnv(path string, values map[string]string) error {
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	var b strings.Builder
	for _, key := range keys {
		fmt.Fprintf(&b, "%s=%s\n", key, values[key])
	}
	return os.WriteFile(path, []byte(b.String()), 0600)
}

func tlsConfig(cfg config) (*tls.Config, string, error) {
	if _, err := os.Stat(cfg.certPath); os.IsNotExist(err) {
		if err := generateSelfSignedCert(cfg.certPath, cfg.keyPath); err != nil {
			return nil, "", err
		}
	}
	cert, err := tls.LoadX509KeyPair(cfg.certPath, cfg.keyPath)
	if err != nil {
		return nil, "", err
	}
	fp := sha256.Sum256(cert.Certificate[0])
	return &tls.Config{
		MinVersion:   tls.VersionTLS12,
		Certificates: []tls.Certificate{cert},
		NextProtos:   []string{protocolName},
	}, hex.EncodeToString(fp[:]), nil
}

func generateSelfSignedCert(certPath, keyPath string) error {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return err
	}
	tpl := x509.Certificate{
		SerialNumber: serial,
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().AddDate(5, 0, 0),
		KeyUsage:     x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"bestgram"},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1")},
	}
	der, err := x509.CreateCertificate(rand.Reader, &tpl, &tpl, &key.PublicKey, key)
	if err != nil {
		return err
	}
	certFile, err := os.OpenFile(certPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return err
	}
	if err := pem.Encode(certFile, &pem.Block{Type: "CERTIFICATE", Bytes: der}); err != nil {
		certFile.Close()
		return err
	}
	certFile.Close()
	keyFile, err := os.OpenFile(keyPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return err
	}
	if err := pem.Encode(keyFile, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(key)}); err != nil {
		keyFile.Close()
		return err
	}
	return keyFile.Close()
}

func newServer(cfg config) (*server, error) {
	block, err := aes.NewCipher(sha256Bytes(append([]byte("enc:"), cfg.seed...)))
	if err != nil {
		return nil, err
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	db, err := sql.Open("sqlite3", cfg.dbPath+"?_busy_timeout=5000&_journal_mode=WAL")
	if err != nil {
		return nil, err
	}
	s := &server{
		cfg:         cfg,
		db:          db,
		sec:         security{seed: cfg.seed, aead: aead},
		clients:     map[*client]struct{}{},
		userClients: map[int64]map[*client]struct{}{},
		channels:    map[string]map[*client]struct{}{},
		limiters:    map[string]*tokenBucket{},
		failures:    map[string]*loginFailure{},
	}
	if err := s.migrate(); err != nil {
		db.Close()
		return nil, err
	}
	return s, nil
}

func sha256Bytes(data []byte) []byte {
	sum := sha256.Sum256(data)
	return sum[:]
}

func (s *server) migrate() error {
	stmts := []string{
		`CREATE TABLE IF NOT EXISTS users (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			login_hash TEXT NOT NULL UNIQUE,
			login_enc TEXT NOT NULL,
			display_enc TEXT NOT NULL,
			password_hash TEXT NOT NULL,
			password_salt TEXT NOT NULL,
			description_enc TEXT NOT NULL DEFAULT '',
			banner_color TEXT NOT NULL DEFAULT '#5865f2',
			status TEXT NOT NULL DEFAULT 'online',
			skin_enc TEXT NOT NULL DEFAULT '',
			created_at INTEGER NOT NULL
		)`,
		`CREATE TABLE IF NOT EXISTS sessions (
			token_hash TEXT PRIMARY KEY,
			user_id INTEGER NOT NULL,
			expires_at INTEGER NOT NULL,
			created_at INTEGER NOT NULL,
			FOREIGN KEY(user_id) REFERENCES users(id)
		)`,
		`CREATE TABLE IF NOT EXISTS channels (
			name TEXT PRIMARY KEY
		)`,
		`CREATE TABLE IF NOT EXISTS messages (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			room_type TEXT NOT NULL,
			room_key TEXT NOT NULL,
			sender_id INTEGER NOT NULL,
			body_enc TEXT NOT NULL,
			created_at INTEGER NOT NULL,
			FOREIGN KEY(sender_id) REFERENCES users(id)
		)`,
		`CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(room_type, room_key, id DESC)`,
		`CREATE TABLE IF NOT EXISTS friends (
			user_low INTEGER NOT NULL,
			user_high INTEGER NOT NULL,
			status TEXT NOT NULL,
			requested_by INTEGER NOT NULL,
			created_at INTEGER NOT NULL,
			updated_at INTEGER NOT NULL,
			PRIMARY KEY(user_low, user_high)
		)`,
	}
	for _, stmt := range stmts {
		if _, err := s.db.Exec(stmt); err != nil {
			return err
		}
	}
	for _, ch := range generalChannels {
		if _, err := s.db.Exec(`INSERT OR IGNORE INTO channels(name) VALUES(?)`, ch); err != nil {
			return err
		}
	}
	return nil
}

func (s *server) handleConn(conn net.Conn) {
	addr := conn.RemoteAddr().String()
	ip, _, _ := net.SplitHostPort(addr)
	if !s.allow("connect:"+ip, 8, 0.25) {
		conn.Close()
		return
	}
	c := &client{
		srv:      s,
		conn:     conn,
		addr:     ip,
		status:   "online",
		channels: map[string]bool{},
		send:     make(chan envelope, 64),
		closed:   make(chan struct{}),
	}
	s.addClient(c)
	defer s.removeClient(c)
	go c.writeLoop()
	c.sendEnv(envelope{"type": "hello", "protocol": protocolName, "channels": generalChannels})

	scanner := bufio.NewScanner(conn)
	scanner.Buffer(make([]byte, 4096), maxLineBytes)
	for scanner.Scan() {
		var msg incoming
		if err := json.Unmarshal(scanner.Bytes(), &msg); err != nil {
			c.sendError("", "bad_json", "Invalid JSON.")
			continue
		}
		s.dispatch(c, msg)
	}
}

func (s *server) dispatch(c *client, msg incoming) {
	switch msg.Type {
	case "auth.register":
		s.handleRegister(c, msg)
	case "auth.login":
		s.handleLogin(c, msg)
	case "auth.resume":
		s.handleResume(c, msg)
	case "auth.login.change":
		s.handleLoginChange(c, msg)
	case "auth.password.change":
		s.handlePasswordChange(c, msg)
	case "auth.logout":
		s.handleLogout(c, msg)
	case "channel.join":
		s.handleChannelJoin(c, msg)
	case "channel.history":
		s.handleHistory(c, msg, "channel", normalizeChannel(msg.Channel))
	case "message.send":
		s.handleMessageSend(c, msg)
	case "dm.open":
		s.handleDMOpen(c, msg)
	case "dm.history":
		if c.userID == 0 {
			c.sendError(msg.RequestID, "auth_required", "Login required.")
			return
		}
		other := msg.UserID
		if other == 0 {
			other, _ = strconv.ParseInt(msg.Target, 10, 64)
		}
		s.handleHistory(c, msg, "dm", dmKey(c.userID, other))
	case "user.search":
		s.handleUserSearch(c, msg)
	case "user.profile.update":
		s.handleProfileUpdate(c, msg)
	case "user.status.update":
		s.handleStatusUpdate(c, msg)
	case "friend.request":
		s.handleFriendRequest(c, msg)
	case "friend.accept":
		s.handleFriendSet(c, msg, "accepted")
	case "friend.decline":
		s.handleFriendSet(c, msg, "declined")
	case "friend.remove":
		s.handleFriendRemove(c, msg)
	case "friend.list":
		s.handleFriendList(c, msg)
	default:
		c.sendError(msg.RequestID, "unknown_type", "Unknown message type.")
	}
}

func (s *server) handleLogout(c *client, msg incoming) {
	oldUserID := c.userID
	s.mu.Lock()
	if oldUserID != 0 && s.userClients[oldUserID] != nil {
		delete(s.userClients[oldUserID], c)
		if len(s.userClients[oldUserID]) == 0 {
			delete(s.userClients, oldUserID)
		}
	}
	for ch, set := range s.channels {
		delete(set, c)
		if len(set) == 0 {
			delete(s.channels, ch)
		}
	}
	c.userID = 0
	c.login = ""
	c.display = ""
	c.status = ""
	c.invisible = false
	s.mu.Unlock()
	c.sendEnv(ok(msg.RequestID, "auth.logout.ok"))
	if oldUserID != 0 {
		s.broadcastPresence(oldUserID)
	}
}

func (s *server) handleRegister(c *client, msg incoming) {
	login := strings.TrimSpace(msg.Login)
	if !validLogin(login) || len(msg.Password) < 6 {
		c.sendError(msg.RequestID, "invalid_register", "Login must be 3-24 chars and password at least 6 chars.")
		return
	}
	if !s.allow("auth:"+c.addr, 5, 0.05) {
		c.sendError(msg.RequestID, "rate_limited", "Too many auth attempts.")
		return
	}
	salt := randomB64(16)
	hash := hashPassword(msg.Password, salt)
	now := time.Now().Unix()
	res, err := s.db.Exec(`INSERT INTO users(login_hash, login_enc, display_enc, password_hash, password_salt, created_at)
		VALUES(?,?,?,?,?,?)`, s.lookup(login), s.sec.encrypt(login), s.sec.encrypt(login), hash, salt, now)
	if err != nil {
		c.sendError(msg.RequestID, "login_taken", "Login is already taken.")
		return
	}
	id, _ := res.LastInsertId()
	token := s.createSession(id)
	s.authClient(c, id, login, login, "online", false)
	c.sendEnv(envelope{"type": "auth.ok", "request_id": msg.RequestID, "user": s.userPublic(id), "session_token": token})
	s.sendInitial(c)
}

func (s *server) handleLogin(c *client, msg incoming) {
	login := strings.TrimSpace(msg.Login)
	key := "login:" + c.addr + ":" + strings.ToLower(login)
	if fail := s.failureLocked(key); fail != "" {
		c.sendError(msg.RequestID, "login_locked", fail)
		return
	}
	if !s.allow("auth:"+c.addr, 5, 0.05) {
		c.sendError(msg.RequestID, "rate_limited", "Too many auth attempts.")
		return
	}
	var id int64
	var loginEnc, displayEnc, passHash, salt, status string
	err := s.db.QueryRow(`SELECT id, login_enc, display_enc, password_hash, password_salt, status FROM users WHERE login_hash=?`, s.lookup(login)).
		Scan(&id, &loginEnc, &displayEnc, &passHash, &salt, &status)
	if err != nil || !verifyPassword(msg.Password, salt, passHash) {
		s.addFailure(key)
		c.sendError(msg.RequestID, "bad_login", "Invalid login or password.")
		return
	}
	s.clearFailure(key)
	token := s.createSession(id)
	realLogin := s.sec.decrypt(loginEnc)
	display := s.sec.decrypt(displayEnc)
	s.authClient(c, id, realLogin, display, status, status == "invisible")
	c.sendEnv(envelope{"type": "auth.ok", "request_id": msg.RequestID, "user": s.userPublic(id), "session_token": token})
	s.sendInitial(c)
}

func (s *server) handleResume(c *client, msg incoming) {
	if msg.Token == "" || !s.allow("resume:"+c.addr, 10, 0.1) {
		c.sendError(msg.RequestID, "bad_session", "Invalid session.")
		return
	}
	var id int64
	var expires int64
	err := s.db.QueryRow(`SELECT user_id, expires_at FROM sessions WHERE token_hash=?`, s.sessionHash(msg.Token)).Scan(&id, &expires)
	if err != nil || expires < time.Now().Unix() {
		c.sendError(msg.RequestID, "bad_session", "Invalid session.")
		return
	}
	u := s.userPublic(id)
	s.authClient(c, id, strAny(u["login"]), strAny(u["display"]), strAny(u["status"]), strAny(u["status"]) == "invisible")
	c.sendEnv(envelope{"type": "auth.ok", "request_id": msg.RequestID, "user": u, "session_token": msg.Token})
	s.sendInitial(c)
}

func (s *server) verifyCurrentPassword(c *client, password string) bool {
	var passHash, salt string
	err := s.db.QueryRow(`SELECT password_hash, password_salt FROM users WHERE id=?`, c.userID).Scan(&passHash, &salt)
	return err == nil && verifyPassword(password, salt, passHash)
}

func (s *server) handleLoginChange(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	newLogin := strings.TrimSpace(msg.NewLogin)
	if !validLogin(newLogin) {
		c.sendError(msg.RequestID, "bad_login", "Login must be 3-24 chars.")
		return
	}
	if !s.allow(fmt.Sprintf("account:%d", c.userID), 4, 0.03) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	if !s.verifyCurrentPassword(c, msg.Password) {
		c.sendError(msg.RequestID, "bad_password", "Invalid password.")
		return
	}
	_, err := s.db.Exec(`UPDATE users SET login_hash=?, login_enc=?, display_enc=? WHERE id=?`,
		s.lookup(newLogin), s.sec.encrypt(newLogin), s.sec.encrypt(newLogin), c.userID)
	if err != nil {
		c.sendError(msg.RequestID, "login_taken", "Login is already taken.")
		return
	}
	c.login = newLogin
	c.display = newLogin
	c.sendEnv(envelope{"type": "auth.login.change.ok", "request_id": msg.RequestID, "user": s.userPublic(c.userID)})
	s.broadcastPresence(c.userID)
}

func (s *server) handlePasswordChange(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	if len(msg.NewPassword) < 6 {
		c.sendError(msg.RequestID, "bad_password", "Password must be at least 6 chars.")
		return
	}
	if !s.allow(fmt.Sprintf("account:%d", c.userID), 4, 0.03) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	if !s.verifyCurrentPassword(c, msg.Password) {
		c.sendError(msg.RequestID, "bad_password", "Invalid password.")
		return
	}
	salt := randomB64(16)
	if _, err := s.db.Exec(`UPDATE users SET password_hash=?, password_salt=? WHERE id=?`, hashPassword(msg.NewPassword, salt), salt, c.userID); err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not update password.")
		return
	}
	c.sendEnv(envelope{"type": "auth.password.change.ok", "request_id": msg.RequestID})
}

func (s *server) handleChannelJoin(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	ch := normalizeChannel(msg.Channel)
	if !isGeneralChannel(ch) {
		c.sendError(msg.RequestID, "bad_channel", "Unknown channel.")
		return
	}
	s.mu.Lock()
	if s.channels[ch] == nil {
		s.channels[ch] = map[*client]struct{}{}
	}
	s.channels[ch][c] = struct{}{}
	c.channels[ch] = true
	s.mu.Unlock()
	c.sendEnv(envelope{"type": "channel.join.ok", "request_id": msg.RequestID, "channel": ch})
	s.handleHistory(c, msg, "channel", ch)
	s.sendPresenceSnapshot(c)
}

func (s *server) handleHistory(c *client, msg incoming, roomType, roomKey string) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	if roomType == "dm" {
		parts := strings.Split(roomKey, ":")
		if len(parts) != 2 {
			c.sendError(msg.RequestID, "bad_dm", "Invalid DM.")
			return
		}
		other, _ := strconv.ParseInt(parts[1], 10, 64)
		if parts[0] == strconv.FormatInt(c.userID, 10) {
			other, _ = strconv.ParseInt(parts[1], 10, 64)
		} else {
			other, _ = strconv.ParseInt(parts[0], 10, 64)
		}
		if !s.areFriends(c.userID, other) {
			c.sendError(msg.RequestID, "not_friends", "DM is available for friends only.")
			return
		}
	}
	limit := msg.Limit
	if limit <= 0 || limit > maxHistory {
		limit = maxHistory
	}
	var rows *sql.Rows
	var err error
	if msg.BeforeID > 0 {
		rows, err = s.db.Query(`SELECT id, sender_id, body_enc, created_at FROM messages WHERE room_type=? AND room_key=? AND id < ? ORDER BY id DESC LIMIT ?`, roomType, roomKey, msg.BeforeID, limit)
	} else {
		rows, err = s.db.Query(`SELECT id, sender_id, body_enc, created_at FROM messages WHERE room_type=? AND room_key=? ORDER BY id DESC LIMIT ?`, roomType, roomKey, limit)
	}
	if err != nil {
		c.sendError(msg.RequestID, "db_error", "History unavailable.")
		return
	}
	defer rows.Close()
	var messages []envelope
	for rows.Next() {
		var id, sender, created int64
		var bodyEnc string
		if rows.Scan(&id, &sender, &bodyEnc, &created) == nil {
			messages = append(messages, envelope{"id": id, "sender": s.userPublic(sender), "text": s.sec.decrypt(bodyEnc), "created_at": created})
		}
	}
	for i, j := 0, len(messages)-1; i < j; i, j = i+1, j-1 {
		messages[i], messages[j] = messages[j], messages[i]
	}
	c.sendEnv(envelope{"type": "history", "request_id": msg.RequestID, "room_type": roomType, "room_key": roomKey, "messages": messages})
}

func (s *server) handleMessageSend(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	text := strings.TrimSpace(msg.Text)
	if text == "" || len([]rune(text)) > maxMsgLen {
		c.sendError(msg.RequestID, "bad_message", "Message is empty or too long.")
		return
	}
	if !s.allow(fmt.Sprintf("msg:%d", c.userID), 8, 0.8) || !s.allow("msgip:"+c.addr, 20, 1.5) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	roomType := msg.TargetType
	roomKey := normalizeChannel(msg.Target)
	if roomType == "channel" {
		if !isGeneralChannel(roomKey) {
			c.sendError(msg.RequestID, "bad_channel", "Unknown channel.")
			return
		}
	} else if roomType == "dm" {
		other, _ := strconv.ParseInt(msg.Target, 10, 64)
		if other == 0 || !s.areFriends(c.userID, other) {
			c.sendError(msg.RequestID, "not_friends", "DM is available for friends only.")
			return
		}
		roomKey = dmKey(c.userID, other)
	} else {
		c.sendError(msg.RequestID, "bad_target", "Unknown message target.")
		return
	}
	now := time.Now().Unix()
	res, err := s.db.Exec(`INSERT INTO messages(room_type, room_key, sender_id, body_enc, created_at) VALUES(?,?,?,?,?)`,
		roomType, roomKey, c.userID, s.sec.encrypt(text), now)
	if err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not save message.")
		return
	}
	id, _ := res.LastInsertId()
	ev := envelope{"type": "message.new", "id": id, "room_type": roomType, "room_key": roomKey, "sender": s.userPublic(c.userID), "text": text, "created_at": now}
	c.sendEnv(envelope{"type": "message.send.ok", "request_id": msg.RequestID, "id": id})
	if roomType == "channel" {
		s.broadcastChannel(roomKey, ev)
	} else {
		s.broadcastUsers(dmUsers(roomKey), ev)
	}
}

func (s *server) handleDMOpen(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	other := msg.UserID
	if other == 0 {
		other, _ = strconv.ParseInt(msg.Target, 10, 64)
	}
	if other == 0 || !s.areFriends(c.userID, other) {
		c.sendError(msg.RequestID, "not_friends", "DM is available for friends only.")
		return
	}
	key := dmKey(c.userID, other)
	c.sendEnv(envelope{"type": "dm.open.ok", "request_id": msg.RequestID, "room_key": key, "user": s.userPublic(other)})
	s.handleHistory(c, msg, "dm", key)
}

func (s *server) handleUserSearch(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	if !s.allow(fmt.Sprintf("search:%d", c.userID), 6, 0.2) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	q := strings.ToLower(strings.TrimSpace(msg.Query))
	var users []envelope
	s.mu.Lock()
	ids := make([]int64, 0, len(s.userClients))
	for id, set := range s.userClients {
		if id != c.userID && len(set) > 0 {
			ids = append(ids, id)
		}
	}
	s.mu.Unlock()
	for _, id := range ids {
		u := s.userPublic(id)
		if strAny(u["status"]) == "invisible" {
			continue
		}
		if q == "" || strings.Contains(strings.ToLower(strAny(u["display"])), q) || strings.Contains(strings.ToLower(strAny(u["login"])), q) {
			users = append(users, u)
		}
	}
	c.sendEnv(envelope{"type": "user.search.results", "request_id": msg.RequestID, "users": users})
}

func (s *server) handleProfileUpdate(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	if !s.allow(fmt.Sprintf("profile:%d", c.userID), 4, 0.05) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	if len(msg.Description) > 240 || len(msg.Skin) > 4096 {
		c.sendError(msg.RequestID, "bad_profile", "Profile is too large.")
		return
	}
	color := msg.BannerColor
	if color == "" {
		color = "#5865f2"
	}
	if _, err := s.db.Exec(`UPDATE users SET description_enc=?, banner_color=?, skin_enc=? WHERE id=?`,
		s.sec.encrypt(msg.Description), color, s.sec.encrypt(msg.Skin), c.userID); err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not update profile.")
		return
	}
	c.sendEnv(envelope{"type": "user.profile.update.ok", "request_id": msg.RequestID, "user": s.userPublic(c.userID)})
	s.broadcastPresence(c.userID)
}

func (s *server) handleStatusUpdate(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	status := normalizeStatus(msg.Status)
	_, _ = s.db.Exec(`UPDATE users SET status=? WHERE id=?`, status, c.userID)
	c.status = status
	c.invisible = status == "invisible"
	c.sendEnv(envelope{"type": "user.status.update.ok", "request_id": msg.RequestID, "status": status})
	s.broadcastPresence(c.userID)
}

func (s *server) handleFriendRequest(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	if !s.allow(fmt.Sprintf("friend:%d", c.userID), 5, 0.05) {
		c.sendError(msg.RequestID, "rate_limited", "Slow down.")
		return
	}
	other := msg.UserID
	if other == 0 || other == c.userID {
		c.sendError(msg.RequestID, "bad_friend", "Invalid user.")
		return
	}
	lo, hi := pair(c.userID, other)
	now := time.Now().Unix()
	_, err := s.db.Exec(`INSERT INTO friends(user_low,user_high,status,requested_by,created_at,updated_at) VALUES(?,?,?,?,?,?)
		ON CONFLICT(user_low,user_high) DO UPDATE SET status='pending', requested_by=?, updated_at=?`,
		lo, hi, "pending", c.userID, now, now, c.userID, now)
	if err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not request friend.")
		return
	}
	c.sendEnv(envelope{"type": "friend.request.ok", "request_id": msg.RequestID, "user": s.userPublic(other)})
	s.broadcastUsers([]int64{other}, envelope{"type": "friend.request.new", "user": s.userPublic(c.userID)})
}

func (s *server) handleFriendSet(c *client, msg incoming, status string) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	other := msg.UserID
	lo, hi := pair(c.userID, other)
	res, err := s.db.Exec(`UPDATE friends SET status=?, updated_at=? WHERE user_low=? AND user_high=? AND status='pending' AND requested_by<>?`,
		status, time.Now().Unix(), lo, hi, c.userID)
	if err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not update friend.")
		return
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		c.sendError(msg.RequestID, "bad_friend", "No pending request.")
		return
	}
	c.sendEnv(envelope{"type": "friend.update.ok", "request_id": msg.RequestID})
	s.broadcastUsers([]int64{other}, envelope{"type": "friend.update", "status": status, "user": s.userPublic(c.userID)})
	s.handleFriendList(c, incoming{RequestID: msg.RequestID})
}

func (s *server) handleFriendRemove(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	other := msg.UserID
	lo, hi := pair(c.userID, other)
	_, _ = s.db.Exec(`DELETE FROM friends WHERE user_low=? AND user_high=?`, lo, hi)
	c.sendEnv(envelope{"type": "friend.remove.ok", "request_id": msg.RequestID})
	s.broadcastUsers([]int64{other}, envelope{"type": "friend.update", "status": "removed", "user": s.userPublic(c.userID)})
}

func (s *server) handleFriendList(c *client, msg incoming) {
	if c.userID == 0 {
		c.sendError(msg.RequestID, "auth_required", "Login required.")
		return
	}
	rows, err := s.db.Query(`SELECT user_low,user_high,status,requested_by FROM friends WHERE user_low=? OR user_high=?`, c.userID, c.userID)
	if err != nil {
		c.sendError(msg.RequestID, "db_error", "Could not load friends.")
		return
	}
	defer rows.Close()
	var friends []envelope
	var pendingIn []envelope
	var pendingOut []envelope
	for rows.Next() {
		var lo, hi, requestedBy int64
		var status string
		if rows.Scan(&lo, &hi, &status, &requestedBy) != nil {
			continue
		}
		other := hi
		if other == c.userID {
			other = lo
		}
		item := s.userPublic(other)
		item["friend_status"] = status
		if status == "accepted" {
			friends = append(friends, item)
		} else if requestedBy == c.userID {
			pendingOut = append(pendingOut, item)
		} else {
			pendingIn = append(pendingIn, item)
		}
	}
	c.sendEnv(envelope{"type": "friend.list", "request_id": msg.RequestID, "friends": friends, "pending_in": pendingIn, "pending_out": pendingOut})
}

func (s *server) sendInitial(c *client) {
	s.handleFriendList(c, incoming{})
	s.sendPresenceSnapshot(c)
	s.broadcastPresence(c.userID)
}

func (s *server) authClient(c *client, id int64, login, display, status string, invisible bool) {
	c.userID = id
	c.login = login
	c.display = display
	c.status = normalizeStatus(status)
	c.invisible = invisible
	s.mu.Lock()
	if s.userClients[id] == nil {
		s.userClients[id] = map[*client]struct{}{}
	}
	s.userClients[id][c] = struct{}{}
	s.mu.Unlock()
}

func (s *server) addClient(c *client) {
	s.mu.Lock()
	s.clients[c] = struct{}{}
	s.mu.Unlock()
}

func (s *server) removeClient(c *client) {
	close(c.closed)
	c.conn.Close()
	s.mu.Lock()
	delete(s.clients, c)
	if c.userID != 0 && s.userClients[c.userID] != nil {
		delete(s.userClients[c.userID], c)
		if len(s.userClients[c.userID]) == 0 {
			delete(s.userClients, c.userID)
		}
	}
	for ch, set := range s.channels {
		delete(set, c)
		if len(set) == 0 {
			delete(s.channels, ch)
		}
	}
	userID := c.userID
	s.mu.Unlock()
	if userID != 0 {
		s.broadcastPresence(userID)
	}
}

func (c *client) writeLoop() {
	for {
		select {
		case msg := <-c.send:
			data, _ := json.Marshal(msg)
			if _, err := c.conn.Write(append(data, '\n')); err != nil {
				c.conn.Close()
				return
			}
		case <-c.closed:
			return
		}
	}
}

func (c *client) sendEnv(env envelope) {
	select {
	case c.send <- env:
	default:
		c.conn.Close()
	}
}

func (c *client) sendError(req, code, message string) {
	c.sendEnv(envelope{"type": "error", "request_id": req, "code": code, "message": message})
}

func (s *server) broadcastChannel(ch string, env envelope) {
	s.mu.Lock()
	targets := make([]*client, 0, len(s.channels[ch]))
	for c := range s.channels[ch] {
		targets = append(targets, c)
	}
	s.mu.Unlock()
	for _, c := range targets {
		c.sendEnv(env)
	}
}

func (s *server) broadcastUsers(ids []int64, env envelope) {
	s.mu.Lock()
	var targets []*client
	for _, id := range ids {
		for c := range s.userClients[id] {
			targets = append(targets, c)
		}
	}
	s.mu.Unlock()
	for _, c := range targets {
		c.sendEnv(env)
	}
}

func (s *server) broadcastPresence(userID int64) {
	u := s.userPublic(userID)
	if strAny(u["status"]) == "invisible" || !s.isOnline(userID) {
		u["online"] = false
	}
	s.mu.Lock()
	targets := make([]*client, 0, len(s.clients))
	for c := range s.clients {
		if c.userID != 0 {
			targets = append(targets, c)
		}
	}
	s.mu.Unlock()
	for _, c := range targets {
		c.sendEnv(envelope{"type": "presence.update", "user": u})
	}
}

func (s *server) sendPresenceSnapshot(c *client) {
	s.mu.Lock()
	ids := make([]int64, 0, len(s.userClients))
	for id := range s.userClients {
		ids = append(ids, id)
	}
	s.mu.Unlock()
	users := make([]envelope, 0, len(ids))
	for _, id := range ids {
		u := s.userPublic(id)
		if strAny(u["status"]) != "invisible" {
			users = append(users, u)
		}
	}
	c.sendEnv(envelope{"type": "presence.snapshot", "users": users})
}

func (s *server) userPublic(id int64) envelope {
	var loginEnc, displayEnc, descEnc, banner, status, skinEnc string
	err := s.db.QueryRow(`SELECT login_enc, display_enc, description_enc, banner_color, status, skin_enc FROM users WHERE id=?`, id).
		Scan(&loginEnc, &displayEnc, &descEnc, &banner, &status, &skinEnc)
	if err != nil {
		return envelope{"id": id, "login": "unknown", "display": "unknown", "online": false, "status": "offline"}
	}
	return envelope{
		"id":           id,
		"login":        s.sec.decrypt(loginEnc),
		"display":      s.sec.decrypt(displayEnc),
		"description":  s.sec.decrypt(descEnc),
		"banner_color": banner,
		"status":       normalizeStatus(status),
		"skin":         s.sec.decrypt(skinEnc),
		"online":       s.isOnline(id) && status != "invisible",
	}
}

func (s *server) isOnline(id int64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.userClients[id]) > 0
}

func (s *server) lookup(login string) string {
	return s.hmacHex("login:" + strings.ToLower(strings.TrimSpace(login)))
}

func (s *server) sessionHash(token string) string {
	return s.hmacHex("session:" + token)
}

func (s *server) hmacHex(value string) string {
	m := hmac.New(sha256.New, s.cfg.seed)
	_, _ = m.Write([]byte(value))
	return hex.EncodeToString(m.Sum(nil))
}

func (s *server) createSession(userID int64) string {
	token := randomB64(32)
	now := time.Now()
	_, _ = s.db.Exec(`INSERT INTO sessions(token_hash,user_id,expires_at,created_at) VALUES(?,?,?,?)`,
		s.sessionHash(token), userID, now.AddDate(0, 6, 0).Unix(), now.Unix())
	return token
}

func (s security) encrypt(plain string) string {
	if plain == "" {
		return ""
	}
	nonce := make([]byte, s.aead.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		panic(err)
	}
	out := s.aead.Seal(nonce, nonce, []byte(plain), nil)
	return base64.RawStdEncoding.EncodeToString(out)
}

func (s security) decrypt(enc string) string {
	if enc == "" {
		return ""
	}
	data, err := base64.RawStdEncoding.DecodeString(enc)
	if err != nil || len(data) < s.aead.NonceSize() {
		return ""
	}
	nonce := data[:s.aead.NonceSize()]
	cipherText := data[s.aead.NonceSize():]
	plain, err := s.aead.Open(nil, nonce, cipherText, nil)
	if err != nil {
		return ""
	}
	return string(plain)
}

func hashPassword(password, saltB64 string) string {
	salt, _ := base64.RawStdEncoding.DecodeString(saltB64)
	hash := argon2.IDKey([]byte(password), salt, 3, 64*1024, 2, 32)
	return base64.RawStdEncoding.EncodeToString(hash)
}

func verifyPassword(password, saltB64, expected string) bool {
	actual := hashPassword(password, saltB64)
	return hmac.Equal([]byte(actual), []byte(expected))
}

func randomB64(n int) string {
	buf := make([]byte, n)
	if _, err := io.ReadFull(rand.Reader, buf); err != nil {
		panic(err)
	}
	return base64.RawStdEncoding.EncodeToString(buf)
}

func (s *server) allow(key string, burst float64, refillPerSec float64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	b := s.limiters[key]
	if b == nil {
		b = &tokenBucket{tokens: burst, last: now}
		s.limiters[key] = b
	}
	elapsed := now.Sub(b.last).Seconds()
	b.tokens += elapsed * refillPerSec
	if b.tokens > burst {
		b.tokens = burst
	}
	b.last = now
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}

func (s *server) failureLocked(key string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	f := s.failures[key]
	if f == nil || time.Now().After(f.until) {
		return ""
	}
	return fmt.Sprintf("Try again after %s.", f.until.Format(time.RFC3339))
}

func (s *server) addFailure(key string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	f := s.failures[key]
	if f == nil {
		f = &loginFailure{}
		s.failures[key] = f
	}
	f.count++
	f.lastHit = time.Now()
	if f.count >= 4 {
		delay := time.Duration(1<<(min(f.count-4, 6))) * time.Minute
		f.until = time.Now().Add(delay)
	}
}

func (s *server) clearFailure(key string) {
	s.mu.Lock()
	delete(s.failures, key)
	s.mu.Unlock()
}

func validLogin(login string) bool {
	if len(login) < 3 || len(login) > 24 {
		return false
	}
	for _, r := range login {
		if !(r == '_' || r == '-' || r == '.' || r >= '0' && r <= '9' || r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z') {
			return false
		}
	}
	return true
}

func normalizeChannel(ch string) string {
	ch = strings.ToLower(strings.TrimSpace(ch))
	ch = strings.TrimPrefix(ch, "#")
	return ch
}

func isGeneralChannel(ch string) bool {
	for _, c := range generalChannels {
		if c == ch {
			return true
		}
	}
	return false
}

func normalizeStatus(status string) string {
	switch strings.ToLower(strings.TrimSpace(status)) {
	case "idle":
		return "idle"
	case "dnd", "do_not_disturb":
		return "dnd"
	case "invisible":
		return "invisible"
	default:
		return "online"
	}
}

func dmKey(a, b int64) string {
	lo, hi := pair(a, b)
	return fmt.Sprintf("%d:%d", lo, hi)
}

func dmUsers(key string) []int64 {
	parts := strings.Split(key, ":")
	if len(parts) != 2 {
		return nil
	}
	a, _ := strconv.ParseInt(parts[0], 10, 64)
	b, _ := strconv.ParseInt(parts[1], 10, 64)
	return []int64{a, b}
}

func pair(a, b int64) (int64, int64) {
	if a < b {
		return a, b
	}
	return b, a
}

func (s *server) areFriends(a, b int64) bool {
	lo, hi := pair(a, b)
	var status string
	err := s.db.QueryRow(`SELECT status FROM friends WHERE user_low=? AND user_high=?`, lo, hi).Scan(&status)
	return err == nil && status == "accepted"
}

func strAny(v any) string {
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}

func ok(req, typ string) envelope {
	return envelope{"type": typ, "request_id": req}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func (s *server) shutdown(ctx context.Context) error {
	done := make(chan struct{})
	go func() {
		s.db.Close()
		close(done)
	}()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-done:
		return nil
	}
}
