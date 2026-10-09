-- Initial schema. Times are unix milliseconds. Telegram ids are stored as
-- INTEGER (64-bit). Every group-owned row carries group_id; queries always
-- filter by it (group isolation).

CREATE TABLE users (
  id INTEGER PRIMARY KEY,                 -- Telegram user id (stable identity)
  first_name TEXT NOT NULL DEFAULT '',
  last_name TEXT NOT NULL DEFAULT '',
  username TEXT,
  updated_at INTEGER NOT NULL
);

CREATE TABLE groups (
  id INTEGER PRIMARY KEY AUTOINCREMENT,   -- internal id, survives group->supergroup migration
  chat_id INTEGER NOT NULL UNIQUE,        -- current Telegram chat id
  title TEXT NOT NULL DEFAULT '',
  room_token TEXT NOT NULL UNIQUE,        -- opaque startapp token (not a secret: access needs membership)
  status TEXT NOT NULL DEFAULT 'active',  -- active | bot_removed | disabled
  bot_is_admin INTEGER NOT NULL DEFAULT 0,
  bot_rights TEXT NOT NULL DEFAULT '{}',
  settings TEXT NOT NULL DEFAULT '{}',
  quota_bytes INTEGER NOT NULL,
  lobby_message_id INTEGER,
  last_announce_at INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE group_chat_ids (            -- every chat id a group has had (migrations)
  chat_id INTEGER PRIMARY KEY,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  created_at INTEGER NOT NULL
);

CREATE TABLE memberships (               -- last verified Telegram membership
  group_id INTEGER NOT NULL REFERENCES groups(id),
  user_id INTEGER NOT NULL,
  tg_status TEXT NOT NULL,               -- creator|administrator|member|restricted|left|kicked
  is_member INTEGER NOT NULL,
  is_admin INTEGER NOT NULL DEFAULT 0,
  checked_at INTEGER NOT NULL,
  PRIMARY KEY (group_id, user_id)
);

CREATE TABLE roles (                     -- application roles (host, deputies)
  group_id INTEGER NOT NULL REFERENCES groups(id),
  user_id INTEGER NOT NULL,
  role TEXT NOT NULL,                    -- host | deputy
  succession_order INTEGER,
  granted_by INTEGER,
  granted_at INTEGER NOT NULL,
  PRIMARY KEY (group_id, user_id, role)
);

CREATE TABLE auth_sessions (             -- our own bearer tokens (hash stored, never the token)
  token_hash TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  kind TEXT NOT NULL,                    -- miniapp | browser
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  revoked_at INTEGER
);
CREATE INDEX auth_sessions_user ON auth_sessions(user_id, group_id);

CREATE TABLE handoffs (                  -- one-time links to continue in an external browser
  token_hash TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  expires_at INTEGER NOT NULL,
  used_at INTEGER
);

CREATE TABLE blobs (                     -- private content-addressed ROM storage
  sha256 TEXT PRIMARY KEY,
  size INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE TABLE uploads (                   -- upload pipeline (Telegram or web)
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  user_id INTEGER NOT NULL,
  source TEXT NOT NULL,                  -- telegram | web
  file_name TEXT NOT NULL,
  size INTEGER,
  tg_file_id TEXT,
  tg_file_unique_id TEXT,
  tg_message_id INTEGER,
  status TEXT NOT NULL,                  -- queued | downloading | validating | done | failed
  game_id INTEGER,
  error TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX uploads_group ON uploads(group_id, created_at);

CREATE TABLE games (                     -- shelf entries (per group)
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  blob_sha256 TEXT NOT NULL REFERENCES blobs(sha256),
  file_name TEXT NOT NULL,
  display_name TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'game',     -- game | bios | parent (dependency-only entries)
  system TEXT,                           -- fbneo_cps12 | fbneo_neogeo | fceumm
  set_name TEXT,                         -- FBNeo romset (driver) name
  parent_set TEXT,
  bios_set TEXT,
  players INTEGER,
  mode TEXT,                             -- versus | coop | single | turns-shared | turns-multi | collab
  genre TEXT,
  year TEXT,
  manufacturer TEXT,
  status TEXT NOT NULL,                  -- ready | needs_dependency | needs_attention | rejected | removed
  compat TEXT NOT NULL DEFAULT 'untested', -- untested | working | needs_attention
  validation TEXT NOT NULL DEFAULT '{}', -- JSON: checks, findings, missing deps
  metadata TEXT NOT NULL DEFAULT '{}',   -- editable metadata (notes, artwork key, tags)
  uploader_id INTEGER,
  uploaded_at INTEGER NOT NULL,
  removed_at INTEGER,
  removed_by INTEGER,
  last_played_at INTEGER,
  play_count INTEGER NOT NULL DEFAULT 0,
  UNIQUE (group_id, blob_sha256)
);
CREATE INDEX games_group ON games(group_id, status);
CREATE INDEX games_set ON games(group_id, set_name);

CREATE TABLE favorites (
  group_id INTEGER NOT NULL REFERENCES groups(id),
  user_id INTEGER NOT NULL,
  game_id INTEGER NOT NULL REFERENCES games(id),
  created_at INTEGER NOT NULL,
  PRIMARY KEY (group_id, user_id, game_id)
);

CREATE TABLE game_sessions (             -- each emulated run
  id INTEGER PRIMARY KEY,                -- random session id (also used on the wire)
  group_id INTEGER NOT NULL REFERENCES groups(id),
  game_id INTEGER NOT NULL REFERENCES games(id),
  compat_key TEXT NOT NULL,              -- rom + core build + options + adapter version
  adapter_id TEXT,
  fresh INTEGER NOT NULL,                -- 1 = fresh boot, 0 = resumed from checkpoint
  resumed_from INTEGER,
  started_at INTEGER NOT NULL,
  ended_at INTEGER,
  end_reason TEXT
);

CREATE TABLE checkpoints (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  game_id INTEGER NOT NULL REFERENCES games(id),
  session_id INTEGER NOT NULL,
  frame INTEGER NOT NULL,
  file TEXT NOT NULL,
  compat_key TEXT NOT NULL,
  size INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  valid INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX checkpoints_group ON checkpoints(group_id, game_id, created_at);

CREATE TABLE control_segments (          -- who controlled which port, and when
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  session_id INTEGER NOT NULL,
  group_id INTEGER NOT NULL,
  port INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  start_frame INTEGER NOT NULL,
  end_frame INTEGER,
  start_score INTEGER,
  end_score INTEGER,
  end_reason TEXT
);
CREATE INDEX control_segments_session ON control_segments(session_id, port);

CREATE TABLE game_events (               -- adapter and manual events (idempotent by key)
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL,
  session_id INTEGER NOT NULL,
  event_key TEXT NOT NULL UNIQUE,
  type TEXT NOT NULL,
  frame INTEGER,
  source TEXT NOT NULL,                  -- adapter | manual | moderator
  data TEXT NOT NULL DEFAULT '{}',
  created_at INTEGER NOT NULL
);

CREATE TABLE matches (                   -- fighter results
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  game_id INTEGER NOT NULL,
  session_id INTEGER NOT NULL,
  event_key TEXT NOT NULL UNIQUE,
  p1_user INTEGER,
  p2_user INTEGER,
  winner_user INTEGER,
  result TEXT NOT NULL,                  -- win | draw | abandoned | interrupted | void
  verification TEXT NOT NULL,            -- verified | manual | adjudicated
  compat_key TEXT NOT NULL,
  counts INTEGER NOT NULL DEFAULT 1,     -- 0 when excluded from stats (mixed control, void)
  created_at INTEGER NOT NULL,
  corrected_by INTEGER,
  correction_reason TEXT
);
CREATE INDEX matches_group ON matches(group_id, game_id, created_at);

CREATE TABLE scores (                    -- score records with attribution
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  game_id INTEGER NOT NULL,
  session_id INTEGER NOT NULL,
  event_key TEXT NOT NULL UNIQUE,
  kind TEXT NOT NULL,                    -- individual | seat | team | collaborative
  user_id INTEGER,                       -- set only when one person earned it all
  port INTEGER,
  participants TEXT NOT NULL DEFAULT '[]', -- [{user, from, to, points}]
  score INTEGER NOT NULL,
  verification TEXT NOT NULL,            -- verified | manual
  compat_key TEXT NOT NULL,
  run_flags TEXT NOT NULL DEFAULT '{}',  -- {fresh, continues, settings}
  created_at INTEGER NOT NULL,
  voided_at INTEGER,
  voided_by INTEGER,
  void_reason TEXT
);
CREATE INDEX scores_group ON scores(group_id, game_id, score);

CREATE TABLE audit_log (                 -- role changes, moderation, corrections
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL,
  actor_id INTEGER,                      -- null = system
  action TEXT NOT NULL,
  target_id INTEGER,
  reason TEXT,
  data TEXT NOT NULL DEFAULT '{}',
  created_at INTEGER NOT NULL
);
CREATE INDEX audit_group ON audit_log(group_id, created_at);

CREATE TABLE chat_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  group_id INTEGER NOT NULL REFERENCES groups(id),
  user_id INTEGER NOT NULL,
  text TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  deleted_by INTEGER,
  deleted_at INTEGER
);
CREATE INDEX chat_group ON chat_messages(group_id, id);

CREATE TABLE telegram_updates (          -- processed update ids (idempotency)
  update_id INTEGER PRIMARY KEY,
  received_at INTEGER NOT NULL
);

CREATE TABLE room_state (                -- server-authoritative room state for restart recovery
  group_id INTEGER PRIMARY KEY REFERENCES groups(id),
  state TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE user_settings (             -- controls, touch layout, display prefs
  user_id INTEGER PRIMARY KEY,
  settings TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE kv (                        -- small server state (e.g. polling offset)
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
