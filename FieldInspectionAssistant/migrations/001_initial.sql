-- FieldInspectionAssistant initial schema (v3)，与 DatabaseManager 内置迁移保持一致。
-- 用于离线部署时手工初始化或文档核对；正常运行由应用内 migrate() 自动执行。
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS schema_meta (version INTEGER NOT NULL);
INSERT INTO schema_meta(version) SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_meta);

CREATE TABLE IF NOT EXISTS app_state (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, name TEXT NOT NULL, role TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS assets (id TEXT PRIMARY KEY, asset_code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, location TEXT, model TEXT, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS inspection_templates (id TEXT PRIMARY KEY, name TEXT NOT NULL, version INTEGER NOT NULL DEFAULT 1, definition_json TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS inspection_tasks (id TEXT PRIMARY KEY, task_code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, status TEXT NOT NULL, assignee_id TEXT, asset_id TEXT, planned_at TEXT, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS inspection_records (id TEXT PRIMARY KEY, task_id TEXT, asset_id TEXT, template_name TEXT NOT NULL, result TEXT NOT NULL, answer TEXT, image_path TEXT, status TEXT NOT NULL DEFAULT 'draft', created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS defects (id TEXT PRIMARY KEY, defect_code TEXT UNIQUE NOT NULL, record_id TEXT, asset_id TEXT, title TEXT NOT NULL, status TEXT NOT NULL, priority TEXT NOT NULL, assignee_id TEXT, created_at TEXT NOT NULL, closed_at TEXT);
CREATE TABLE IF NOT EXISTS defect_events (id TEXT PRIMARY KEY, defect_id TEXT NOT NULL, status TEXT NOT NULL, note TEXT, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS attachments (id TEXT PRIMARY KEY, record_id TEXT, defect_id TEXT, path TEXT NOT NULL, sha256 TEXT, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS audit_logs (id TEXT PRIMARY KEY, action TEXT NOT NULL, entity TEXT NOT NULL, entity_id TEXT, detail TEXT, created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS sync_outbox (id TEXT PRIMARY KEY, entity TEXT NOT NULL, entity_id TEXT NOT NULL, operation TEXT NOT NULL, payload_json TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'pending', attempts INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL);

-- v3：检查项结果明细、缺陷扩展字段、事件类型/操作人、附件类型与索引
CREATE TABLE IF NOT EXISTS inspection_check_items (
    id TEXT PRIMARY KEY,
    record_id TEXT NOT NULL,
    item_key TEXT NOT NULL,
    item_name TEXT NOT NULL,
    item_type TEXT NOT NULL,
    unit TEXT,
    result TEXT NOT NULL DEFAULT '',
    value_text TEXT,
    threshold_text TEXT,
    is_abnormal INTEGER NOT NULL DEFAULT 0,
    note TEXT,
    image_path TEXT,
    created_at TEXT NOT NULL,
    UNIQUE(record_id, item_key)
);
ALTER TABLE defects ADD COLUMN description TEXT;
ALTER TABLE defects ADD COLUMN due_at TEXT;
ALTER TABLE defect_events ADD COLUMN event_type TEXT;
ALTER TABLE defect_events ADD COLUMN actor_id TEXT;
ALTER TABLE attachments ADD COLUMN kind TEXT;
CREATE INDEX IF NOT EXISTS idx_check_items_record ON inspection_check_items(record_id);
CREATE INDEX IF NOT EXISTS idx_defects_status ON defects(status);
CREATE INDEX IF NOT EXISTS idx_records_asset_time ON inspection_records(asset_id, created_at);

UPDATE schema_meta SET version=3;