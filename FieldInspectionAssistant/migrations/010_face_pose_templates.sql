CREATE TABLE face_pose_templates(person_id TEXT NOT NULL REFERENCES face_people(id) ON DELETE CASCADE, pose INTEGER NOT NULL CHECK(pose BETWEEN 1 AND 4), feature BLOB NOT NULL CHECK(length(feature)=512), updated_at TEXT NOT NULL, PRIMARY KEY(person_id,pose))
UPDATE schema_meta SET version=10
