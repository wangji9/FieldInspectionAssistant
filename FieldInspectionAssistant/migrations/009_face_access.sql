CREATE TABLE face_people(id TEXT PRIMARY KEY, code TEXT NOT NULL UNIQUE COLLATE NOCASE, name TEXT NOT NULL, active INTEGER NOT NULL CHECK(active IN (0,1)), model TEXT NOT NULL, feature BLOB NOT NULL CHECK(length(feature)=512), created_at TEXT NOT NULL, updated_at TEXT NOT NULL)
CREATE TABLE face_events(id INTEGER PRIMARY KEY, person_id TEXT, code TEXT NOT NULL, name TEXT NOT NULL, result TEXT NOT NULL CHECK(result IN ('match','unknown')), similarity REAL, captured_at TEXT NOT NULL)
UPDATE schema_meta SET version=9
