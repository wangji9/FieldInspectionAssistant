#include "DatabaseManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

DatabaseManager::DatabaseManager() {
    connectionName_ = QStringLiteral("field_inspection_%1").arg(reinterpret_cast<quintptr>(this));
}

DatabaseManager::~DatabaseManager() {
    if (db_.isOpen()) db_.close();
    const QString name = connectionName_;
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool DatabaseManager::open(QString *error) {
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataRoot.isEmpty()) {
        if (error) *error = QStringLiteral("无法确定本地数据目录");
        return false;
    }
    QDir().mkpath(dataRoot);
    evidenceDir_ = QDir(dataRoot).filePath(QStringLiteral("evidence"));
    QDir().mkpath(evidenceDir_);
    databasePath_ = QDir(dataRoot).filePath(QStringLiteral("field_inspection.sqlite"));

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(databasePath_);
    if (!db_.open()) {
        if (error) *error = db_.lastError().text();
        return false;
    }
    return migrate(error);
}

bool DatabaseManager::migrate(QString *error) {
    if (!exec(QStringLiteral("PRAGMA journal_mode=WAL"), error)) return false;
    if (!exec(QStringLiteral("PRAGMA foreign_keys=ON"), error)) return false;
    if (!exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_meta (version INTEGER NOT NULL)"), error)) return false;
    QSqlQuery versionQuery(db_);
    if (!versionQuery.exec(QStringLiteral("SELECT version FROM schema_meta LIMIT 1"))) {
        if (error) *error = errorText(versionQuery);
        return false;
    }
    int version = versionQuery.next() ? versionQuery.value(0).toInt() : 0;
    if (version == 0) {
        if (!exec(QStringLiteral("INSERT INTO schema_meta(version) VALUES (0)"), error)) return false;
    }
    if (version < 1) {
        const QStringList tables = {
            QStringLiteral("CREATE TABLE IF NOT EXISTS app_state (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, name TEXT NOT NULL, role TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS assets (id TEXT PRIMARY KEY, asset_code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, location TEXT, model TEXT, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS inspection_templates (id TEXT PRIMARY KEY, name TEXT NOT NULL, version INTEGER NOT NULL DEFAULT 1, definition_json TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS inspection_tasks (id TEXT PRIMARY KEY, task_code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, status TEXT NOT NULL, assignee_id TEXT, asset_id TEXT, planned_at TEXT, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS inspection_records (id TEXT PRIMARY KEY, task_id TEXT, asset_id TEXT, template_name TEXT NOT NULL, result TEXT NOT NULL, answer TEXT, image_path TEXT, status TEXT NOT NULL DEFAULT 'draft', created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS defects (id TEXT PRIMARY KEY, defect_code TEXT UNIQUE NOT NULL, record_id TEXT, asset_id TEXT, title TEXT NOT NULL, status TEXT NOT NULL, priority TEXT NOT NULL, assignee_id TEXT, created_at TEXT NOT NULL, closed_at TEXT)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS defect_events (id TEXT PRIMARY KEY, defect_id TEXT NOT NULL, status TEXT NOT NULL, note TEXT, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS attachments (id TEXT PRIMARY KEY, record_id TEXT, defect_id TEXT, path TEXT NOT NULL, sha256 TEXT, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS audit_logs (id TEXT PRIMARY KEY, action TEXT NOT NULL, entity TEXT NOT NULL, entity_id TEXT, detail TEXT, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS sync_outbox (id TEXT PRIMARY KEY, entity TEXT NOT NULL, entity_id TEXT NOT NULL, operation TEXT NOT NULL, payload_json TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'pending', attempts INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL)")
        };
        db_.transaction();
        for (const QString &sql : tables) {
            if (!exec(sql, error)) { db_.rollback(); return false; }
        }
        QSqlQuery update(db_);
        update.prepare(QStringLiteral("UPDATE schema_meta SET version=1"));
        if (!update.exec()) { if (error) *error = errorText(update); db_.rollback(); return false; }
        if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
        version = 1;
    }
    if (version < 2) {
        QSqlQuery alter(db_);
        if (!alter.exec(QStringLiteral("ALTER TABLE inspection_tasks ADD COLUMN asset_id TEXT")) &&
            !alter.lastError().text().contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive)) {
            if (error) *error = errorText(alter);
            return false;
        }
        QSqlQuery update(db_); update.prepare(QStringLiteral("UPDATE schema_meta SET version=2"));
        if (!update.exec()) { if (error) *error = errorText(update); return false; }
        version = 2;
    }
    if (version < 3) {
        const QStringList ddl = {
            QStringLiteral("CREATE TABLE IF NOT EXISTS inspection_check_items (id TEXT PRIMARY KEY, record_id TEXT NOT NULL, item_key TEXT NOT NULL, item_name TEXT NOT NULL, item_type TEXT NOT NULL, unit TEXT, result TEXT NOT NULL DEFAULT '', value_text TEXT, threshold_text TEXT, is_abnormal INTEGER NOT NULL DEFAULT 0, note TEXT, image_path TEXT, created_at TEXT NOT NULL, UNIQUE(record_id, item_key))"),
            QStringLiteral("ALTER TABLE defects ADD COLUMN description TEXT"),
            QStringLiteral("ALTER TABLE defects ADD COLUMN due_at TEXT"),
            QStringLiteral("ALTER TABLE defect_events ADD COLUMN event_type TEXT"),
            QStringLiteral("ALTER TABLE defect_events ADD COLUMN actor_id TEXT"),
            QStringLiteral("ALTER TABLE attachments ADD COLUMN kind TEXT"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_check_items_record ON inspection_check_items(record_id)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_defects_status ON defects(status)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_records_asset_time ON inspection_records(asset_id, created_at)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_records_task ON inspection_records(task_id)")
        };
        db_.transaction();
        for (const QString &sql : ddl) {
            if (!exec(sql, error)) { db_.rollback(); return false; }
        }
        QSqlQuery update(db_); update.prepare(QStringLiteral("UPDATE schema_meta SET version=3"));
        if (!update.exec()) { if (error) *error = errorText(update); db_.rollback(); return false; }
        if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
        version = 3;
    }
    return true;
}

QVector<InspectionRecord> DatabaseManager::loadInspectionRecords(const QString &filter, QString *error) const {
    QVector<InspectionRecord> records;
    QSqlQuery query(db_);
    QString sql = QStringLiteral("SELECT r.id, r.created_at, r.template_name, r.result, r.answer, r.image_path, "
                                 "r.task_id, r.asset_id, COALESCE(a.name,''), COALESCE(t.task_code,''), r.status "
                                 "FROM inspection_records r LEFT JOIN assets a ON a.id=r.asset_id "
                                 "LEFT JOIN inspection_tasks t ON t.id=r.task_id");
    if (!filter.isEmpty()) {
        const QStringList parts = filter.split(QLatin1Char('|'));
        QStringList where;
        if (parts.contains(QStringLiteral("abnormal"))) where << QStringLiteral("r.result LIKE '%异常%' OR r.result LIKE '%缺陷%'");
        if (parts.contains(QStringLiteral("draft"))) where << QStringLiteral("r.status='draft'");
        if (parts.contains(QStringLiteral("submitted"))) where << QStringLiteral("r.status='submitted'");
        if (!where.isEmpty()) sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" OR "));
    }
    sql += QStringLiteral(" ORDER BY r.created_at DESC");
    if (!query.exec(sql)) { if (error) *error = errorText(query); return records; }
    while (query.next()) {
        InspectionRecord r;
        r.id = query.value(0).toString(); r.time = query.value(1).toString();
        r.templateName = query.value(2).toString(); r.result = query.value(3).toString();
        r.answer = query.value(4).toString(); r.imagePath = query.value(5).toString();
        r.taskId = query.value(6).toString(); r.assetId = query.value(7).toString();
        r.assetName = query.value(8).toString(); r.taskCode = query.value(9).toString();
        r.status = query.value(10).toString();
        records.push_back(r);
    }
    return records;
}

bool DatabaseManager::insertInspection(const InspectionRecord &record, QString *error) {
    if (!db_.transaction()) { if (error) *error = db_.lastError().text(); return false; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO inspection_records(id, task_id, asset_id, template_name, result, answer, image_path, status, created_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, 'draft', ?)"));
    query.addBindValue(record.id); query.addBindValue(record.taskId); query.addBindValue(record.assetId);
    query.addBindValue(record.templateName); query.addBindValue(record.result); query.addBindValue(record.answer);
    query.addBindValue(record.imagePath); query.addBindValue(record.time);
    if (!query.exec()) { db_.rollback(); if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("create"), QStringLiteral("inspection_record"), record.id, QStringLiteral("创建巡检记录草稿"), error)) { db_.rollback(); return false; }
    if (!enqueueSync(QStringLiteral("inspection_record"), record.id, QStringLiteral("create"), record.id, error)) { db_.rollback(); return false; }
    if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
    return true;
}

bool DatabaseManager::updateRecordStatus(const QString &recordId, const QString &status, const QString &result,
                                         const QString &answer, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE inspection_records SET status=?, result=?, answer=? WHERE id=?"));
    query.addBindValue(status); query.addBindValue(result); query.addBindValue(answer); query.addBindValue(recordId);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QStringLiteral("update"), QStringLiteral("inspection_record"), recordId,
                       QStringLiteral("状态:%1 结论:%2").arg(status, result), error);
}

int DatabaseManager::countRecords(const QString &datePrefix) const {
    QSqlQuery query(db_);
    if (datePrefix.isEmpty()) {
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM inspection_records WHERE status='submitted'"))) return 0;
    } else {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM inspection_records WHERE status='submitted' AND created_at LIKE ?"));
        query.addBindValue(datePrefix + QLatin1Char('%'));
        if (!query.exec()) return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::upsertCheckItem(const CheckItemResult &item, const QString &recordId, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO inspection_check_items(id, record_id, item_key, item_name, item_type, unit, result, value_text, threshold_text, is_abnormal, note, image_path, created_at) "
                                 "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) "
                                 "ON CONFLICT(record_id, item_key) DO UPDATE SET result=excluded.result, value_text=excluded.value_text, "
                                 "is_abnormal=excluded.is_abnormal, note=excluded.note, image_path=excluded.image_path"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(recordId); query.addBindValue(item.key); query.addBindValue(item.name);
    query.addBindValue(item.type); query.addBindValue(item.unit); query.addBindValue(item.result);
    query.addBindValue(item.value); query.addBindValue(item.thresholdText);
    query.addBindValue(item.abnormal ? 1 : 0); query.addBindValue(item.note); query.addBindValue(item.imagePath);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

QVector<CheckItemResult> DatabaseManager::loadCheckItems(const QString &recordId, QString *error) const {
    QVector<CheckItemResult> items;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT item_key, item_name, item_type, unit, result, value_text, threshold_text, is_abnormal, note, image_path "
                                 "FROM inspection_check_items WHERE record_id=? ORDER BY created_at"));
    query.addBindValue(recordId);
    if (!query.exec()) { if (error) *error = errorText(query); return items; }
    while (query.next()) {
        CheckItemResult it;
        it.key = query.value(0).toString(); it.name = query.value(1).toString();
        it.type = query.value(2).toString(); it.unit = query.value(3).toString();
        it.result = query.value(4).toString(); it.value = query.value(5).toString();
        it.thresholdText = query.value(6).toString(); it.abnormal = query.value(7).toBool();
        it.note = query.value(8).toString(); it.imagePath = query.value(9).toString();
        items.push_back(it);
    }
    return items;
}

QVector<InspectionAsset> DatabaseManager::loadAssets(QString *error) const {
    QVector<InspectionAsset> result;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id, asset_code, name, location, model, created_at FROM assets WHERE active=1 ORDER BY asset_code"))) {
        if (error) *error = errorText(query); return result;
    }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                          query.value(3).toString(), query.value(4).toString(), query.value(5).toString()});
    }
    return result;
}

InspectionAsset DatabaseManager::findAssetByCode(const QString &code, QString *error) const {
    InspectionAsset asset;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id, asset_code, name, location, model, created_at FROM assets WHERE asset_code=? AND active=1"));
    query.addBindValue(code.trimmed());
    if (!query.exec()) { if (error) *error = errorText(query); return asset; }
    if (!query.next()) { if (error) *error = QStringLiteral("未找到设备 %1").arg(code.trimmed()); return asset; }
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toString(), query.value(4).toString(), query.value(5).toString()};
}

InspectionAsset DatabaseManager::findAssetById(const QString &id, QString *error) const {
    InspectionAsset a;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id, asset_code, name, COALESCE(location,''), COALESCE(model,''), created_at FROM assets WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return a; }
    if (!query.next()) { if (error) *error = QStringLiteral("设备不存在"); return a; }
    a.id = query.value(0).toString(); a.code = query.value(1).toString(); a.name = query.value(2).toString();
    a.location = query.value(3).toString(); a.model = query.value(4).toString(); a.createdAt = query.value(5).toString();
    return a;
}
bool DatabaseManager::createAsset(const InspectionAsset &asset, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO assets(id, asset_code, name, location, model, created_at) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(asset.id); query.addBindValue(asset.code); query.addBindValue(asset.name);
    query.addBindValue(asset.location); query.addBindValue(asset.model); query.addBindValue(asset.createdAt);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("create"), QStringLiteral("asset"), asset.id, asset.code, error)) return false;
    return enqueueSync(QStringLiteral("asset"), asset.id, QStringLiteral("create"), asset.code, error);
}

bool DatabaseManager::updateAsset(const QString &id, const QString &name, const QString &location,
                                  const QString &model, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE assets SET name=?, location=?, model=? WHERE id=?"));
    query.addBindValue(name); query.addBindValue(location); query.addBindValue(model); query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QStringLiteral("update"), QStringLiteral("asset"), id, name, error);
}

bool DatabaseManager::setAssetActive(const QString &id, bool active, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE assets SET active=? WHERE id=?"));
    query.addBindValue(active ? 1 : 0); query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QString::fromLatin1(active ? "activate" : "deactivate"), QStringLiteral("asset"), id, QString(), error);
}

QVector<InspectionTask> DatabaseManager::loadTasks(const QString &statusFilter, QString *error) const {
    QVector<InspectionTask> result;
    QSqlQuery query(db_);
    QString sql = QStringLiteral("SELECT t.id, t.task_code, t.name, t.status, t.asset_id, t.planned_at, t.created_at, "
                                 "COALESCE((SELECT name FROM assets a WHERE a.id=t.asset_id),'') "
                                 "FROM inspection_tasks t");
    if (statusFilter == QStringLiteral("today")) {
        sql += QStringLiteral(" WHERE t.planned_at LIKE '") + QDate::currentDate().toString(Qt::ISODate) + QStringLiteral("%'");
    } else if (!statusFilter.isEmpty()) {
        sql += QStringLiteral(" WHERE t.status=?");
    }
    sql += QStringLiteral(" ORDER BY t.planned_at DESC, t.created_at DESC");
    if (!query.prepare(sql)) { if (error) *error = query.lastError().text(); return result; }
    if (!statusFilter.isEmpty() && statusFilter != QStringLiteral("today")) query.addBindValue(statusFilter);
    if (!query.exec()) { if (error) *error = errorText(query); return result; }
    while (query.next()) {
        InspectionTask t;
        t.id = query.value(0).toString(); t.code = query.value(1).toString(); t.name = query.value(2).toString();
        t.status = query.value(3).toString(); t.assetId = query.value(4).toString();
        t.plannedAt = query.value(5).toString(); t.createdAt = query.value(6).toString();
        t.assetName = query.value(7).toString();
        result.push_back(t);
    }
    return result;
}

InspectionTask DatabaseManager::findTask(const QString &id, QString *error) const {
    InspectionTask t;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT t.id, t.task_code, t.name, t.status, t.asset_id, t.planned_at, t.created_at, "
                                 "COALESCE((SELECT name FROM assets a WHERE a.id=t.asset_id),'') "
                                 "FROM inspection_tasks t WHERE t.id=?"));
    query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return t; }
    if (!query.next()) { if (error) *error = QStringLiteral("任务不存在"); return t; }
    t.id = query.value(0).toString(); t.code = query.value(1).toString(); t.name = query.value(2).toString();
    t.status = query.value(3).toString(); t.assetId = query.value(4).toString();
    t.plannedAt = query.value(5).toString(); t.createdAt = query.value(6).toString();
    t.assetName = query.value(7).toString();
    return t;
}

bool DatabaseManager::createTask(const InspectionTask &task, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO inspection_tasks(id, task_code, name, status, assignee_id, asset_id, planned_at, created_at) VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(task.id); query.addBindValue(task.code); query.addBindValue(task.name); query.addBindValue(task.status);
    query.addBindValue(QString()); query.addBindValue(task.assetId); query.addBindValue(task.plannedAt); query.addBindValue(task.createdAt);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("create"), QStringLiteral("inspection_task"), task.id, task.code, error)) return false;
    return enqueueSync(QStringLiteral("inspection_task"), task.id, QStringLiteral("create"), task.code, error);
}

bool DatabaseManager::updateTaskStatus(const QString &taskId, const QString &status, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE inspection_tasks SET status=? WHERE id=?"));
    query.addBindValue(status); query.addBindValue(taskId);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("update"), QStringLiteral("inspection_task"), taskId, status, error)) return false;
    return enqueueSync(QStringLiteral("inspection_task"), taskId, QStringLiteral("update"), status, error);
}

int DatabaseManager::countTasks(const QString &status, const QString &datePrefix) const {
    QSqlQuery query(db_);
    if (datePrefix.isEmpty()) {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM inspection_tasks WHERE status=?"));
        query.addBindValue(status);
    } else {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM inspection_tasks WHERE status=? AND planned_at LIKE ?"));
        query.addBindValue(status);
        query.addBindValue(datePrefix + QLatin1Char('%'));
    }
    if (!query.exec()) return 0;
    return query.next() ? query.value(0).toInt() : 0;
}

TemplateInfo DatabaseManager::defaultTemplate(QString *error) const {
    TemplateInfo t;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id, name, version, definition_json, created_at FROM inspection_templates WHERE active=1 ORDER BY created_at LIMIT 1"))) {
        if (error) *error = errorText(query); return t;
    }
    if (!query.next()) { if (error) *error = QStringLiteral("未配置检查模板"); return t; }
    t.id = query.value(0).toString(); t.name = query.value(1).toString();
    t.version = query.value(2).toInt(); t.definitionJson = query.value(3).toString();
    t.createdAt = query.value(4).toString();
    return t;
}

QVector<TemplateInfo> DatabaseManager::loadTemplates(QString *error) const {
    QVector<TemplateInfo> result;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id, name, version, definition_json, created_at FROM inspection_templates WHERE active=1 ORDER BY created_at"))) {
        if (error) *error = errorText(query); return result;
    }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toInt(),
                          query.value(3).toString(), query.value(4).toString()});
    }
    return result;
}

bool DatabaseManager::updateTemplateThreshold(const QString &templateId, const QString &itemKey,
                                             const QString &minText, const QString &maxText, const QString &unit,
                                             QString *error) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT definition_json FROM inspection_templates WHERE id=?"));
    q.addBindValue(templateId);
    if (!q.exec() || !q.next()) { if (error) *error = errorText(q); return false; }
    QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
    if (!doc.isObject()) { if (error) *error = QStringLiteral("模板定义损坏"); return false; }
    QJsonObject root = doc.object();
    QJsonArray items = root.value(QStringLiteral("items")).toArray();
    bool found = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject it = items.at(i).toObject();
        if (it.value(QStringLiteral("key")).toString() == itemKey) {
            it.insert(QStringLiteral("min"), minText);
            it.insert(QStringLiteral("max"), maxText);
            it.insert(QStringLiteral("unit"), unit);
            QString threshold;
            if (!minText.isEmpty()) threshold = QStringLiteral("≥ %1 %2").arg(minText, unit);
            if (!maxText.isEmpty()) {
                if (!threshold.isEmpty()) threshold = QStringLiteral("%1 ~ ").arg(threshold);
                threshold += QStringLiteral("≤ %1 %2").arg(maxText, unit);
            }
            it.insert(QStringLiteral("thresholdText"), threshold);
            items.replace(i, it);
            found = true;
            break;
        }
    }
    if (!found) { if (error) *error = QStringLiteral("检查项不存在"); return false; }
    root.insert(QStringLiteral("items"), items);
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE inspection_templates SET definition_json=?, version=version+1 WHERE id=?"));
    query.addBindValue(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    query.addBindValue(templateId);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QStringLiteral("update"), QStringLiteral("inspection_template"), templateId, itemKey, error);
}

QVector<InspectionDefect> DatabaseManager::loadDefects(const QString &statusFilter, QString *error) const {
    QVector<InspectionDefect> result;
    QSqlQuery query(db_);
    QString sql = QStringLiteral("SELECT d.id, d.defect_code, d.record_id, d.asset_id, d.title, COALESCE(d.description,''), d.status, d.priority, "
                                 "COALESCE(d.assignee_id,''), COALESCE((SELECT u.name FROM users u WHERE u.id=d.assignee_id),''), "
                                 "COALESCE(d.due_at,''), d.created_at, COALESCE(d.closed_at,'') "
                                 "FROM defects d");
    if (!statusFilter.isEmpty() && statusFilter != QStringLiteral("all")) {
        sql += QStringLiteral(" WHERE d.status=?");
    }
    sql += QStringLiteral(" ORDER BY d.created_at DESC");
    if (!query.prepare(sql)) { if (error) *error = query.lastError().text(); return result; }
    if (!statusFilter.isEmpty() && statusFilter != QStringLiteral("all")) query.addBindValue(statusFilter);
    if (!query.exec()) { if (error) *error = errorText(query); return result; }
    while (query.next()) {
        InspectionDefect d;
        d.id = query.value(0).toString(); d.code = query.value(1).toString(); d.recordId = query.value(2).toString();
        d.assetId = query.value(3).toString(); d.title = query.value(4).toString(); d.description = query.value(5).toString();
        d.status = query.value(6).toString(); d.priority = query.value(7).toString(); d.assigneeId = query.value(8).toString();
        d.assigneeName = query.value(9).toString(); d.dueAt = query.value(10).toString();
        d.createdAt = query.value(11).toString(); d.closedAt = query.value(12).toString();
        result.push_back(d);
    }
    return result;
}

InspectionDefect DatabaseManager::findDefect(const QString &id, QString *error) const {
    InspectionDefect d;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT d.id, d.defect_code, d.record_id, d.asset_id, d.title, COALESCE(d.description,''), d.status, d.priority, "
                                 "COALESCE(d.assignee_id,''), COALESCE((SELECT u.name FROM users u WHERE u.id=d.assignee_id),''), "
                                 "COALESCE(d.due_at,''), d.created_at, COALESCE(d.closed_at,'') "
                                 "FROM defects d WHERE d.id=?"));
    query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return d; }
    if (!query.next()) { if (error) *error = QStringLiteral("缺陷单不存在"); return d; }
    d.id = query.value(0).toString(); d.code = query.value(1).toString(); d.recordId = query.value(2).toString();
    d.assetId = query.value(3).toString(); d.title = query.value(4).toString(); d.description = query.value(5).toString();
    d.status = query.value(6).toString(); d.priority = query.value(7).toString(); d.assigneeId = query.value(8).toString();
    d.assigneeName = query.value(9).toString(); d.dueAt = query.value(10).toString();
    d.createdAt = query.value(11).toString(); d.closedAt = query.value(12).toString();
    return d;
}

bool DatabaseManager::createDefect(const InspectionDefect &defect, QString *error) {
    if (!db_.transaction()) { if (error) *error = db_.lastError().text(); return false; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO defects(id, defect_code, record_id, asset_id, title, description, status, priority, assignee_id, created_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(defect.id); query.addBindValue(defect.code); query.addBindValue(defect.recordId);
    query.addBindValue(defect.assetId); query.addBindValue(defect.title); query.addBindValue(defect.description);
    query.addBindValue(defect.status); query.addBindValue(defect.priority); query.addBindValue(defect.assigneeId);
    query.addBindValue(defect.createdAt);
    if (!query.exec()) { db_.rollback(); if (error) *error = errorText(query); return false; }
    if (!createDefectEvent(defect.id, QStringLiteral("create"), defect.status, QStringLiteral("创建缺陷单"), QString(), error)) { db_.rollback(); return false; }
    if (!appendAudit(QStringLiteral("create"), QStringLiteral("defect"), defect.id, defect.title, error)) { db_.rollback(); return false; }
    if (!enqueueSync(QStringLiteral("defect"), defect.id, QStringLiteral("create"), defect.code, error)) { db_.rollback(); return false; }
    if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
    return true;
}

bool DatabaseManager::updateDefect(const QString &id, const QString &priority, const QString &assigneeId,
                                   const QString &dueAt, const QString &description, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE defects SET priority=?, assignee_id=?, due_at=?, description=? WHERE id=?"));
    query.addBindValue(priority); query.addBindValue(assigneeId); query.addBindValue(dueAt);
    query.addBindValue(description); query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QStringLiteral("update"), QStringLiteral("defect"), id,
                       QStringLiteral("优先级:%1 处理人:%2").arg(priority, assigneeId), error);
}

bool DatabaseManager::updateDefectStatus(const QString &defectId, const QString &status, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE defects SET status=?, closed_at=CASE WHEN ?='closed' THEN ? ELSE closed_at END WHERE id=?"));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    query.addBindValue(status); query.addBindValue(status); query.addBindValue(now); query.addBindValue(defectId);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("update"), QStringLiteral("defect"), defectId, status, error)) return false;
    return enqueueSync(QStringLiteral("defect"), defectId, QStringLiteral("update"), status, error);
}

bool DatabaseManager::transitionDefect(const QString &defectId, const QString &newStatus,
                                       const QString &eventType, const QString &note,
                                       const QString &actorId, QString *error) {
    if (!db_.transaction()) { if (error) *error = db_.lastError().text(); return false; }
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE defects SET status=?, closed_at=CASE WHEN ?='closed' THEN ? ELSE closed_at END WHERE id=?"));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    query.addBindValue(newStatus); query.addBindValue(newStatus); query.addBindValue(now); query.addBindValue(defectId);
    if (!query.exec()) { db_.rollback(); if (error) *error = errorText(query); return false; }
    if (!createDefectEvent(defectId, eventType, newStatus, note, actorId, error)) { db_.rollback(); return false; }
    if (!appendAudit(QStringLiteral("transition"), QStringLiteral("defect"), defectId,
                     QStringLiteral("%1: %2").arg(eventType, note), error)) { db_.rollback(); return false; }
    if (!enqueueSync(QStringLiteral("defect"), defectId, QStringLiteral("update"), newStatus, error)) { db_.rollback(); return false; }
    if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
    return true;
}
bool DatabaseManager::createDefectEvent(const QString &defectId, const QString &eventType, const QString &status,
                                        const QString &note, const QString &actorId, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO defect_events(id, defect_id, event_type, status, note, actor_id, created_at) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(defectId); query.addBindValue(eventType); query.addBindValue(status);
    query.addBindValue(note); query.addBindValue(actorId);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

QVector<DefectEvent> DatabaseManager::loadDefectEvents(const QString &defectId, QString *error) const {
    QVector<DefectEvent> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT e.id, COALESCE(e.event_type,'update'), e.status, COALESCE(e.note,''), "
                                 "COALESCE((SELECT u.name FROM users u WHERE u.id=e.actor_id),''), e.created_at "
                                 "FROM defect_events e WHERE e.defect_id=? ORDER BY e.created_at ASC"));
    query.addBindValue(defectId);
    if (!query.exec()) { if (error) *error = errorText(query); return result; }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                          query.value(3).toString(), query.value(4).toString(), query.value(5).toString()});
    }
    return result;
}

int DatabaseManager::countDefects(const QString &status) const {
    QSqlQuery query(db_);
    if (status.isEmpty()) {
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM defects WHERE status NOT IN ('closed')"))) return 0;
    } else {
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM defects WHERE status=?"));
        query.addBindValue(status);
        if (!query.exec()) return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::createAttachment(const QString &recordId, const QString &defectId, const QString &path,
                                       const QString &sha256, const QString &kind, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO attachments(id, record_id, defect_id, path, sha256, kind, created_at) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(recordId); query.addBindValue(defectId); query.addBindValue(path);
    query.addBindValue(sha256); query.addBindValue(kind);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    if (!appendAudit(QStringLiteral("create"), QStringLiteral("attachment"), path, QStringLiteral("新增证据"), error)) return false;
    return enqueueSync(QStringLiteral("attachment"), path, QStringLiteral("create"), path, error);
}

QVector<AttachmentItem> DatabaseManager::loadAttachments(const QString &recordId, const QString &defectId, QString *error) const {
    QVector<AttachmentItem> result;
    QSqlQuery query(db_);
    QString sql = QStringLiteral("SELECT id, COALESCE(record_id,''), COALESCE(defect_id,''), path, COALESCE(sha256,''), COALESCE(kind,'evidence'), created_at FROM attachments WHERE 1=1");
    if (!recordId.isEmpty()) sql += QStringLiteral(" AND record_id=?");
    if (!defectId.isEmpty()) sql += QStringLiteral(" AND defect_id=?");
    sql += QStringLiteral(" ORDER BY created_at DESC");
    if (!query.prepare(sql)) { if (error) *error = query.lastError().text(); return result; }
    if (!recordId.isEmpty()) query.addBindValue(recordId);
    if (!defectId.isEmpty()) query.addBindValue(defectId);
    if (!query.exec()) { if (error) *error = errorText(query); return result; }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                          query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
                          query.value(6).toString()});
    }
    return result;
}

qint64 DatabaseManager::evidenceBytes() const {
    qint64 total = 0;
    QDir dir(evidenceDir_);
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &f : files) total += f.size();
    return total;
}

QVector<UserInfo> DatabaseManager::loadUsers(QString *error) const {
    QVector<UserInfo> result;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id, name, role, active, created_at FROM users ORDER BY created_at"))) {
        if (error) *error = errorText(query); return result;
    }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                          query.value(3).toBool(), query.value(4).toString()});
    }
    return result;
}

UserInfo DatabaseManager::findUserByName(const QString &name, QString *error) const {
    UserInfo u;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT id, name, role, active, created_at FROM users WHERE name=? AND active=1"));
    query.addBindValue(name.trimmed());
    if (!query.exec()) { if (error) *error = errorText(query); return u; }
    if (!query.next()) { if (error) *error = QStringLiteral("未找到用户"); return u; }
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toBool(), query.value(4).toString()};
}

bool DatabaseManager::createUser(const UserInfo &user, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO users(id, name, role, active, created_at) VALUES(?,?,?,?,?)"));
    query.addBindValue(user.id); query.addBindValue(user.name); query.addBindValue(user.role);
    query.addBindValue(user.active ? 1 : 0); query.addBindValue(user.createdAt);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return appendAudit(QStringLiteral("create"), QStringLiteral("user"), user.id, user.name, error);
}

bool DatabaseManager::enqueueSync(const QString &entity, const QString &entityId, const QString &operation,
                                  const QString &payloadJson, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO sync_outbox(id, entity, entity_id, operation, payload_json, status, attempts, updated_at) VALUES(?,?,?,?,?,?,0,?)"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(entity); query.addBindValue(entityId); query.addBindValue(operation);
    query.addBindValue(payloadJson); query.addBindValue(QStringLiteral("pending"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

QVector<SyncItem> DatabaseManager::pendingSyncItems(QString *error) const {
    QVector<SyncItem> result;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id, entity, entity_id, operation, payload_json, status, attempts, updated_at FROM sync_outbox WHERE status='pending' ORDER BY updated_at ASC"))) {
        if (error) *error = errorText(query); return result;
    }
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                          query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
                          query.value(6).toInt(), query.value(7).toString()});
    }
    return result;
}

bool DatabaseManager::markSyncSent(const QString &id, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE sync_outbox SET status='sent', updated_at=? WHERE id=?"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate)); query.addBindValue(id);
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

int DatabaseManager::pendingSyncCount() const {
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM sync_outbox WHERE status='pending'"))) return 0;
    return query.next() ? query.value(0).toInt() : 0;
}

QString DatabaseManager::state(const QString &key, const QString &fallback) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT value FROM app_state WHERE key=?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) return fallback;
    return query.value(0).toString();
}

bool DatabaseManager::setState(const QString &key, const QString &value, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO app_state(key,value,updated_at) VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at"));
    query.addBindValue(key); query.addBindValue(value);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

bool DatabaseManager::appendAudit(const QString &action, const QString &entity, const QString &entityId,
                                  const QString &detail, QString *error) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO audit_logs(id,action,entity,entity_id,detail,created_at) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(action); query.addBindValue(entity); query.addBindValue(entityId);
    query.addBindValue(detail); query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) { if (error) *error = errorText(query); return false; }
    return true;
}

QVector<QVariantMap> DatabaseManager::recentAudit(int limit, QString *error) const {
    QVector<QVariantMap> result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT action, entity, entity_id, detail, created_at FROM audit_logs ORDER BY created_at DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) { if (error) *error = errorText(query); return result; }
    while (query.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("action"), query.value(0));
        m.insert(QStringLiteral("entity"), query.value(1));
        m.insert(QStringLiteral("entityId"), query.value(2));
        m.insert(QStringLiteral("detail"), query.value(3));
        m.insert(QStringLiteral("createdAt"), query.value(4));
        result.push_back(m);
    }
    return result;
}

QString DatabaseManager::nextCode(const QString &prefix, const QString &dateStamp) const {
    QSqlQuery query(db_);
    const QString table = (prefix == QStringLiteral("DEF")) ? QStringLiteral("defects")
                         : (prefix == QStringLiteral("INSP")) ? QStringLiteral("inspection_tasks")
                                                               : QStringLiteral("inspection_tasks");
    const QString column = (prefix == QStringLiteral("DEF")) ? QStringLiteral("defect_code")
                         : (prefix == QStringLiteral("INSP")) ? QStringLiteral("task_code") : QStringLiteral("task_code");
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 LIKE ?").arg(table, column));
    query.addBindValue(prefix + dateStamp + QStringLiteral("-%"));
    if (!query.exec()) return prefix + dateStamp + QStringLiteral("-001");
    const int n = query.next() ? query.value(0).toInt() : 0;
    return QStringLiteral("%1%2-%3").arg(prefix, dateStamp).arg(n + 1, 3, 10, QLatin1Char('0'));
}

bool DatabaseManager::seedDemoDataIfEmpty(QString *error) {
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM assets"))) { if (error) *error = errorText(q); return false; }
    if (q.next() && q.value(0).toInt() > 0) return true;

    if (!db_.transaction()) { if (error) *error = db_.lastError().text(); return false; }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (!createUser({QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("系统管理员"), QStringLiteral("admin"), true, now}, error)) { db_.rollback(); return false; }
    if (!createUser({QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("巡检员"), QStringLiteral("inspector"), true, now}, error)) { db_.rollback(); return false; }

    QJsonObject sound{{QStringLiteral("key"), QStringLiteral("sound")}, {QStringLiteral("name"), QStringLiteral("设备运行声音正常")}, {QStringLiteral("type"), QStringLiteral("select")}, {QStringLiteral("options"), QStringLiteral("正常|异常")}, {QStringLiteral("min"), QString()}, {QStringLiteral("max"), QString()}, {QStringLiteral("unit"), QString()}, {QStringLiteral("thresholdText"), QString()}};
    QJsonObject leak{{QStringLiteral("key"), QStringLiteral("leak")}, {QStringLiteral("name"), QStringLiteral("无明显泄漏")}, {QStringLiteral("type"), QStringLiteral("select")}, {QStringLiteral("options"), QStringLiteral("正常|异常")}, {QStringLiteral("min"), QString()}, {QStringLiteral("max"), QString()}, {QStringLiteral("unit"), QString()}, {QStringLiteral("thresholdText"), QString()}};
    QJsonObject temp{{QStringLiteral("key"), QStringLiteral("bearing_temp")}, {QStringLiteral("name"), QStringLiteral("电机轴承温度")}, {QStringLiteral("type"), QStringLiteral("measure")}, {QStringLiteral("options"), QString()}, {QStringLiteral("min"), QString()}, {QStringLiteral("max"), QStringLiteral("70")}, {QStringLiteral("unit"), QStringLiteral("°C")}, {QStringLiteral("thresholdText"), QStringLiteral("≤ 70 °C")}};
    QJsonObject vib{{QStringLiteral("key"), QStringLiteral("vibration")}, {QStringLiteral("name"), QStringLiteral("设备振动")}, {QStringLiteral("type"), QStringLiteral("select")}, {QStringLiteral("options"), QStringLiteral("正常|异常")}, {QStringLiteral("min"), QString()}, {QStringLiteral("max"), QString()}, {QStringLiteral("unit"), QString()}, {QStringLiteral("thresholdText"), QString()}};
    QJsonObject tmpl; tmpl.insert(QStringLiteral("items"), QJsonArray{sound, leak, temp, vib});
    QSqlQuery tq(db_);
    tq.prepare(QStringLiteral("INSERT INTO inspection_templates(id, name, version, definition_json, created_at) VALUES(?,?,1,?,?)"));
    tq.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tq.addBindValue(QStringLiteral("泵房设备检查标准"));
    tq.addBindValue(QString::fromUtf8(QJsonDocument(tmpl).toJson(QJsonDocument::Compact)));
    tq.addBindValue(now);
    if (!tq.exec()) { db_.rollback(); if (error) *error = errorText(tq); return false; }

    const QStringList pumpNames{QStringLiteral("1 号循环水泵"), QStringLiteral("2 号循环水泵"), QStringLiteral("1 号给水泵"), QStringLiteral("2 号给水泵"), QStringLiteral("1 号排污泵"), QStringLiteral("2 号排污泵"), QStringLiteral("消防稳压泵"), QStringLiteral("冷却水泵")};
    for (int i = 0; i < pumpNames.size(); ++i) {
        const QString code = QStringLiteral("PUMP-%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        if (!createAsset({QUuid::createUuid().toString(QUuid::WithoutBraces), code, pumpNames.at(i),
                          QStringLiteral("A 区水泵房"), QStringLiteral("IS100-65-200"), now}, error)) { db_.rollback(); return false; }
    }

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (!createTask({QUuid::createUuid().toString(QUuid::WithoutBraces), nextCode(QStringLiteral("INSP"), today),
                     QStringLiteral("泵房每日巡检"), QStringLiteral("pending"), QString(),
                     today + QStringLiteral("T08:00:00"), now}, error)) { db_.rollback(); return false; }

    if (!setState(QStringLiteral("seeded"), QStringLiteral("1"), error)) { db_.rollback(); return false; }
    if (!db_.commit()) { if (error) *error = db_.lastError().text(); return false; }
    return true;
}

bool DatabaseManager::exec(const QString &sql, QString *error) const {
    QSqlQuery query(db_);
    if (query.exec(sql)) return true;
    if (error) *error = errorText(query);
    return false;
}

QString DatabaseManager::errorText(const QSqlQuery &query) const {
    return query.lastError().text().isEmpty() ? db_.lastError().text() : query.lastError().text();
}
