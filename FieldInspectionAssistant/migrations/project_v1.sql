CREATE TABLE project(id TEXT PRIMARY KEY, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', updated_at TEXT NOT NULL);
CREATE TABLE devices(id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE evidence(id TEXT PRIMARY KEY, device_id TEXT NOT NULL REFERENCES devices(id), point TEXT NOT NULL, kind TEXT NOT NULL, title TEXT NOT NULL, path TEXT NOT NULL UNIQUE, captured_ms INTEGER NOT NULL);
CREATE INDEX evidence_capture ON evidence(device_id, point, captured_ms);
CREATE TABLE fusion_events(id TEXT PRIMARY KEY, anchor_id TEXT NOT NULL REFERENCES evidence(id), evidence_ids TEXT NOT NULL, created_at TEXT NOT NULL, UNIQUE(anchor_id, evidence_ids));
CREATE TABLE ai_findings(id TEXT PRIMARY KEY, event_id TEXT NOT NULL REFERENCES fusion_events(id), body TEXT NOT NULL, model TEXT NOT NULL, created_at TEXT NOT NULL);
CREATE TABLE reports(id TEXT PRIMARY KEY, event_id TEXT NOT NULL REFERENCES fusion_events(id), path TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL);
CREATE TABLE activity(id INTEGER PRIMARY KEY, action TEXT NOT NULL, detail TEXT NOT NULL, time TEXT NOT NULL);
PRAGMA user_version = 1;
