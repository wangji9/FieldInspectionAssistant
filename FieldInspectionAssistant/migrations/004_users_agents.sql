-- Applied transactionally by DatabaseManager::migrate(). Existing business rows are preserved.
ALTER TABLE users ADD COLUMN password_hash TEXT NOT NULL DEFAULT '';
ALTER TABLE users ADD COLUMN password_salt TEXT NOT NULL DEFAULT '';
ALTER TABLE users ADD COLUMN password_iterations INTEGER NOT NULL DEFAULT 600000;
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_login_name ON users(name) WHERE password_hash<>'';
CREATE TABLE IF NOT EXISTS agent_conversations(id TEXT PRIMARY KEY,title TEXT NOT NULL,user_id TEXT NOT NULL,created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS agent_messages(id INTEGER PRIMARY KEY,conversation_id TEXT NOT NULL,role TEXT NOT NULL,body TEXT NOT NULL,kind TEXT NOT NULL,created_at TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS agent_messages_conversation ON agent_messages(conversation_id,id);
UPDATE schema_meta SET version=4;
