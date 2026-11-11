#include "ProjectWorkspace.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include "../database/DatabaseManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QPdfWriter>
#include <QSettings>
#include <QSet>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

namespace {
const QString Format = QStringLiteral("miiep.project");
const QMap<QString, QString> EvidenceFolders{
    {"image", "assets/images"}, {"audio", "assets/audio"}, {"thermal", "assets/thermal"},
    {"video", "assets/video"}, {"signal", "signals/raw"}, {"text", "assets/text"}};

QString identifier() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }
bool validName(const QString &name)
{
    return !name.isEmpty() && name.size() <= 80 && name != "." && name != ".."
        && !name.contains('/') && !name.contains('\\');
}
QVariantMap success() { return {{"ok", true}}; }
QVariantMap failure(const QString &text) { return {{"ok", false}, {"error", text}}; }

void formatReport(QTextDocument &document, const QString &markdown)
{
    const auto font = QGuiApplication::font();
    document.setDefaultFont(font);
    const auto features = QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                        | QTextDocument::MarkdownNoHTML;
    document.setMarkdown(markdown, features);
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setFontFamilies(font.families());
    format.setFontFixedPitch(false);
    cursor.mergeCharFormat(format);
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit();
}

bool execute(QSqlDatabase db, const QString &statement, const QVariantList &args = {})
{
    QSqlQuery query(db);
    if (!query.prepare(statement))
        return false;
    for (const auto &arg : args)
        query.addBindValue(arg);
    return query.exec();
}

void closeDatabase(QSqlDatabase &db)
{
    const auto connection = db.connectionName();
    db.close();
    db = {};
    if (!connection.isEmpty())
        QSqlDatabase::removeDatabase(connection);
}

QSqlDatabase connectDatabase(const QString &path, bool readOnly = false)
{
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), identifier());
    if (readOnly)
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    db.setDatabaseName(path);
    if (!db.open() || !execute(db, QStringLiteral("PRAGMA foreign_keys = ON")))
        closeDatabase(db);
    return db;
}

bool managedFile(const QDir &dir, const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return false;
    const QFileInfo info(dir.filePath(path));
    return info.isFile() && info.canonicalFilePath().startsWith(dir.absolutePath() + '/');
}

QVariantMap readProject(const QUrl &folder, QSqlDatabase &database, bool readOnly)
{
    if (!folder.isLocalFile())
        return {};
    const auto path = QFileInfo(folder.toLocalFile()).canonicalFilePath();
    const QDir dir(path);
    if (path.isEmpty() || !managedFile(dir, "project.json") || !managedFile(dir, "database/project.db"))
        return {};
    QFile file(dir.filePath("project.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
        return {};
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    const auto id = manifest.value("id").toString();
    if (manifest.value("format").toString() != Format || manifest.value("version").toInt() != 1 || id.isEmpty())
        return {};
    database = connectDatabase(dir.filePath("database/project.db"), readOnly);
    if (!database.isOpen())
        return {};
    QSqlQuery query(database);
    if (!query.exec("PRAGMA user_version") || !query.next() || query.value(0).toInt() != 1
        || !query.prepare("SELECT name,description,updated_at FROM project WHERE id=?"))
        return {};
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return {};
    return {{"id", id}, {"path", path}, {"name", query.value(0)},
            {"description", query.value(1)}, {"updated_at", query.value(2)}};
}

QString projectsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/projects";
}

const QString DemoFolder = QStringLiteral("设备巡检演示.miproj");

QString classification(const QString &text)
{
    const auto first = text.trimmed().section('\n', 0, 0).trimmed();
    static const QRegularExpression pattern(QStringLiteral("^判定\\s*[:：]\\s*(NORMAL|ABNORMAL|UNKNOWN)$"));
    const auto match = pattern.match(first);
    return match.hasMatch() ? match.captured(1).toLower() : QStringLiteral("unknown");
}

QString classificationLabel(const QString &value)
{
    if (value == "normal") return QStringLiteral("正常");
    if (value == "abnormal") return QStringLiteral("异常");
    return QStringLiteral("待判定");
}
}

ProjectWorkspace::ProjectWorkspace(DatabaseManager *business, SessionManager *session, QObject *parent)
    : QObject(parent), business_(business), session_(session)
{
    connect(session_, &SessionManager::sessionEnding, this, &ProjectWorkspace::closeProject);
    connect(session_, &SessionManager::sessionChanged, this, &ProjectWorkspace::changed);
}

ProjectWorkspace::~ProjectWorkspace() { release(); }
bool ProjectWorkspace::ready() const { return session_->loggedIn() && db_.isOpen(); }
void ProjectWorkspace::release() { closeDatabase(db_); project_.clear(); }

QVariantMap ProjectWorkspace::createProject(const QString &name, const QUrl &parentFolder)
{
    const auto label = name.trimmed();
    if (!session_->loggedIn() || !validName(label))
        return failure(tr("请输入有效的工程名称。"));
    const QString base = parentFolder.isEmpty()
        ? projectsPath()
        : parentFolder.toLocalFile();
    const QString target = label + ".miproj";
    QDir parent(base);
    if (base.isEmpty() || (!parent.exists() && !QDir().mkpath(base)) || parent.exists(target))
        return failure(tr("工程目录不可用，或已存在同名工程。"));

    QTemporaryDir temporary(parent.filePath(".miproj-XXXXXX"));
    if (!temporary.isValid())
        return failure(tr("无法创建工程目录。"));
    const QDir staging(temporary.path());
    auto folders = EvidenceFolders.values();
    folders << "database" << "reports" << "logs" << "signals/features" << "models" << "knowledge" << "tasks";
    for (const auto &folder : folders) {
        if (!staging.mkpath(folder))
            return failure(tr("无法建立工程存储目录。"));
    }

    QFile schema(":/migrations/project_v1.sql");
    if (!schema.open(QIODevice::ReadOnly))
        return failure(tr("无法读取工程数据库结构。"));
    const auto id = identifier();
    auto database = connectDatabase(staging.filePath("database/project.db"));
    bool ok = database.isOpen() && database.transaction();
    for (const auto &statement : schema.readAll().split(';')) {
        if (ok && !statement.trimmed().isEmpty())
            ok = execute(database, QString::fromUtf8(statement));
    }
    ok = ok && execute(database, "INSERT INTO project(id,name,updated_at) VALUES(?,?,?)",
                       {id, label, now()}) && database.commit();
    closeDatabase(database);
    const QJsonObject manifest{{"format", Format}, {"version", 1}, {"id", id}};
    if (!ok || !writeFile(staging.filePath("project.json"), QJsonDocument(manifest).toJson())
        || !parent.rename(staging.dirName(), target))
        return failure(tr("无法保存新工程。"));
    return openProject(QUrl::fromLocalFile(parent.filePath(target)));
}

QVariantMap ProjectWorkspace::openProject(const QUrl &folder)
{
    if (!session_->loggedIn())
        return failure(tr("请先登录。"));
    QSqlDatabase candidate;
    const auto data = readProject(folder, candidate, false);
    if (data.isEmpty()) {
        closeDatabase(candidate);
        return failure(tr("无法打开工程，请选择清单和数据库匹配的 .miproj 目录。"));
    }
    // Keep the portable project format compatible with existing readers.
    if (!candidate.transaction() ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_device_links("
                            "device_id TEXT PRIMARY KEY REFERENCES devices(id),asset_id TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_event_details("
                            "event_id TEXT PRIMARY KEY REFERENCES fusion_events(id),record_id TEXT,"
                            "ai_result TEXT,review_result TEXT)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_manual_events("
                            "event_id TEXT PRIMARY KEY REFERENCES fusion_events(id),observed_ms INTEGER NOT NULL,"
                            "title TEXT NOT NULL,note TEXT NOT NULL,source TEXT NOT NULL,classification TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_event_defects("
                            "event_id TEXT PRIMARY KEY REFERENCES fusion_events(id),defect_id TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_evidence_records("
                            "evidence_id TEXT PRIMARY KEY REFERENCES evidence(id),record_id TEXT NOT NULL)") ||
        !candidate.commit()) {
        candidate.rollback();
        closeDatabase(candidate);
        return failure(tr("无法初始化工程关联数据。"));
    }
    release();
    db_ = candidate;
    project_ = data;
    rememberProject();
    log(tr("打开工程"), data.value("name").toString());
    emit changed();
    return success();
}

QVariantMap ProjectWorkspace::importProject(const QUrl &folder)
{
    if (!session_->loggedIn())
        return failure(tr("请先登录。"));
    QSqlDatabase source;
    const auto project = readProject(folder, source, true);
    if (project.isEmpty()) {
        closeDatabase(source);
        return failure(tr("请选择有效的 .miproj 工程目录。"));
    }
    const QDir input(project.value("path").toString());
    bool complete;
    {
        QSqlQuery check(source);
        complete = check.exec("PRAGMA quick_check") && check.next() && check.value(0).toString() == "ok";
        if (complete)
            complete = check.exec("SELECT path FROM evidence UNION ALL SELECT path FROM reports");
        while (complete && check.next())
            complete = managedFile(input, check.value(0).toString());
    }
    const QDir parent(projectsPath());
    if (!complete || !QDir().mkpath(parent.absolutePath())) {
        closeDatabase(source);
        return failure(tr("工程数据库、证据或报告不完整，或无法创建导入目录。"));
    }
    auto label = project.value("name").toString().trimmed();
    if (!validName(label))
        label = tr("导入工程");
    if (parent.exists(label + ".miproj"))
        label = label.left(67) + '-' + identifier().left(8);
    const auto copied = writeProjectCopy(source, project, QUrl::fromLocalFile(parent.absolutePath()), label,
                                         project.value("description").toString(), false);
    closeDatabase(source);
    if (!copied.value("ok").toBool())
        return copied;
    const auto result = openProject(QUrl::fromLocalFile(copied.value("path").toString()));
    if (result.value("ok").toBool()) {
        log(tr("导入工程"), project_.value("name").toString());
        emit changed();
    }
    return result;
}

QUrl ProjectWorkspace::demoProject() const
{
    const QDir application(QCoreApplication::applicationDirPath());
    for (const auto &relative : {QStringLiteral("examples/"), QStringLiteral("../examples/")}) {
        const QDir demo(application.filePath(relative + DemoFolder));
        if (demo.exists("project.json") && demo.exists("database/project.db"))
            return QUrl::fromLocalFile(demo.absolutePath());
    }
    return {};
}

QVariantMap ProjectWorkspace::openDemoProject()
{
    const QDir projects(projectsPath());
    if (projects.exists(DemoFolder))
        return openProject(QUrl::fromLocalFile(projects.filePath(DemoFolder)));
    return importProject(demoProject());
}

QVariantMap ProjectWorkspace::saveProject(const QString &name, const QString &description)
{
    const auto label = name.trimmed();
    const auto updated = now();
    if (!ready() || label.isEmpty() || label.size() > 80 || description.size() > 4000
        || !execute(db_, "UPDATE project SET name=?,description=?,updated_at=? WHERE id=?",
                    {label, description, updated, project_.value("id")}))
        return failure(tr("无法保存工程属性，请检查名称与存储。"));
    project_["name"] = label;
    project_["description"] = description;
    project_["updated_at"] = updated;
    rememberProject();
    log(tr("保存工程"), label);
    emit changed();
    return success();
}

void ProjectWorkspace::closeProject()
{
    if (db_.isOpen())
        log(tr("关闭工程"), project_.value("name").toString());
    release();
    emit changed();
}

QVariantList ProjectWorkspace::recentProjects() const
{
    if (!session_->loggedIn())
        return {};
    return QSettings().value("projects/recent/" + session_->userId()).toList();
}

void ProjectWorkspace::rememberProject()
{
    auto recent = recentProjects();
    const auto path = project_.value("path").toString();
    for (int i = recent.size() - 1; i >= 0; --i)
        if (recent.at(i).toMap().value("path").toString() == path) recent.removeAt(i);
    recent.prepend(QVariantMap{{"path", path}, {"url", QUrl::fromLocalFile(path)},
                               {"name", project_.value("name")}});
    while (recent.size() > 8) recent.removeLast();
    QSettings().setValue("projects/recent/" + session_->userId(), recent);
}

QVariantMap ProjectWorkspace::copyProject(const QUrl &folder, const QString &name,
                                          const QString &description, bool archive)
{
    if (!ready())
        return failure(tr("请先打开工程。"));
    const auto result = writeProjectCopy(db_, project_, folder, name, description, archive);
    if (!result.value("ok").toBool())
        return result;
    const auto path = result.value("path").toString();
    if (archive) {
        log(tr("归档工程"), path);
        emit changed();
        return result;
    }
    return openProject(QUrl::fromLocalFile(path));
}

QVariantMap ProjectWorkspace::writeProjectCopy(const QSqlDatabase &sourceDb, const QVariantMap &sourceProject,
                                                const QUrl &folder, const QString &name,
                                                const QString &description, bool archive) const
{
    if (!folder.isLocalFile() || description.size() > 4000)
        return failure(tr("请打开工程并选择存放目录。"));
    const QDir source(sourceProject.value("path").toString());
    QDir parent(folder.toLocalFile());
    const auto destination = parent.canonicalPath();
    if (destination.isEmpty() || destination == source.absolutePath()
        || destination.startsWith(source.absolutePath() + '/'))
        return failure(tr("请选择工程目录之外的存放位置。"));
    const auto label = name.trimmed();
    if (!archive && !validName(label))
        return failure(tr("请输入有效的副本名称。"));
    const auto target = archive
        ? source.dirName().left(55) + "-archive-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".miproj"
        : label + ".miproj";
    if (parent.exists(target))
        return failure(tr("目标工程已经存在。"));

    QTemporaryDir temporary(parent.filePath(".miproj-copy-XXXXXX"));
    if (!temporary.isValid())
        return failure(tr("无法创建工程副本。"));
    const QDir staging(temporary.path());
    QDirIterator entries(source.absolutePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next();
        const auto info = entries.fileInfo();
        const auto relative = source.relativeFilePath(info.absoluteFilePath());
        if (relative == "project.json" || relative == "database/project.db"
            || relative == "database/project.db-wal" || relative == "database/project.db-shm"
            || relative == "database/project.db-journal")
            continue;
        if (info.isSymLink() || (info.isDir() ? !staging.mkpath(relative)
            : !info.isFile() || !QFile::copy(info.absoluteFilePath(), staging.filePath(relative))))
            return failure(tr("无法复制工程文件：%1").arg(relative));
    }
    const auto databasePath = staging.filePath("database/project.db");
    if (!staging.mkpath("database") || !execute(sourceDb, "VACUUM INTO ?", {databasePath}))
        return failure(tr("无法生成一致的工程数据库副本。"));
    QString id = sourceProject.value("id").toString();
    if (!archive) {
        const auto oldId = id;
        id = identifier();
        auto db = connectDatabase(databasePath);
        const bool updated = db.isOpen() && execute(db, "UPDATE project SET id=?,name=?,description=?,updated_at=? WHERE id=?",
                                                     {id, label, description, now(), oldId});
        closeDatabase(db);
        if (!updated)
            return failure(tr("无法写入副本工程属性。"));
    }
    const QJsonObject manifest{{"format", Format}, {"version", 1}, {"id", id}};
    if (!writeFile(staging.filePath("project.json"), QJsonDocument(manifest).toJson())
        || !parent.rename(staging.dirName(), target))
        return failure(tr("无法完成工程副本。"));
    return {{"ok", true}, {"path", parent.filePath(target)}};
}

QVariantList ProjectWorkspace::rows(const QString &sql, const QVariantList &args) const
{
    QVariantList result;
    if (!ready())
        return result;
    QSqlQuery query(db_);
    if (!query.prepare(sql))
        return result;
    for (const auto &arg : args)
        query.addBindValue(arg);
    if (!query.exec())
        return result;
    const auto fields = query.record();
    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < fields.count(); ++i)
            row.insert(fields.fieldName(i), query.value(i));
        result.append(row);
    }
    return result;
}

void ProjectWorkspace::log(const QString &action, const QString &detail)
{
    execute(db_, "INSERT INTO activity(action,detail,time) VALUES(?,?,?)", {action, detail, now()});
}

QVariantList ProjectWorkspace::devices() const
{
    return rows("SELECT d.id,d.name,l.asset_id FROM devices d LEFT JOIN project_device_links l ON l.device_id=d.id "
                "ORDER BY d.name");
}
QVariantList ProjectWorkspace::evidence() const
{
    auto evidence = rows("SELECT e.*,d.name device,s.record_id,datetime(captured_ms/1000,'unixepoch','localtime') captured "
                         "FROM evidence e JOIN devices d ON d.id=e.device_id "
                         "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id ORDER BY captured_ms DESC,e.id");
    const AccessPolicy access(business_, session_);
    QHash<QString, bool> visibleRecords;
    for (auto &entry : evidence) {
        auto row = entry.toMap();
        const auto record = row.value("record_id").toString();
        if (record.isEmpty()) continue;
        if (!visibleRecords.contains(record)) visibleRecords.insert(record, access.record(record));
        if (!visibleRecords.value(record)) {
            row.remove("record_id");
            entry = row;
        }
    }
    return evidence;
}
QVariantList ProjectWorkspace::events() const
{
    auto result = rows("SELECT f.id,f.created_at,COALESCE(n.title,e.title) title,d.name device,e.point,e.device_id,f.anchor_id, "
                       "m.record_id, l.asset_id,m.ai_result,m.review_result,x.defect_id,"
                       "n.source,n.note, COALESCE(n.observed_ms,e.captured_ms) observed_ms,"
                       "datetime(COALESCE(n.observed_ms,e.captured_ms)/1000,'unixepoch','localtime') observed,"
                       "COALESCE(m.review_result,NULLIF(n.classification,'unknown'),m.ai_result,'unknown') classification,"
                       "EXISTS(SELECT 1 FROM ai_findings a WHERE a.event_id=f.id) analyzed FROM fusion_events f "
                       "JOIN evidence e ON e.id=f.anchor_id JOIN devices d ON d.id=e.device_id "
                       "LEFT JOIN project_event_details m ON m.event_id=f.id "
                       "LEFT JOIN project_manual_events n ON n.event_id=f.id "
                       "LEFT JOIN project_event_defects x ON x.event_id=f.id "
                       "LEFT JOIN project_device_links l ON l.device_id=e.device_id ORDER BY observed_ms DESC,f.id");
    const AccessPolicy access(business_, session_);
    for (auto &value : result) {
        auto row = value.toMap();
        row["classificationLabel"] = classificationLabel(row.value("classification").toString());
        if (!row.value("record_id").toString().isEmpty() && !access.record(row.value("record_id").toString())) {
            row.remove("record_id");
            row.remove("asset_id");
        }
        if (!row.value("defect_id").toString().isEmpty() && !access.defect(row.value("defect_id").toString()))
            row.remove("defect_id");
        value = row;
    }
    return result;
}
QVariantList ProjectWorkspace::reports() const { return rows("SELECT * FROM reports ORDER BY created_at DESC,rowid DESC"); }
QVariantList ProjectWorkspace::logs() const
{
    return rows("SELECT action,detail,time FROM activity ORDER BY id DESC LIMIT 200");
}

QVariantMap ProjectWorkspace::evidenceFile(const QString &evidenceId) const
{
    const auto found = rows("SELECT title,path,kind FROM evidence WHERE id=?", {evidenceId});
    if (found.isEmpty())
        return {};
    auto item = found.first().toMap();
    const QDir dir(project_.value("path").toString());
    const auto path = item.take("path").toString();
    if (!managedFile(dir, path))
        return {};
    const QFileInfo file(dir.filePath(path));
    const auto suffix = file.suffix().toLower();
    item["url"] = QUrl::fromLocalFile(file.absoluteFilePath());
    item["localPath"] = file.absoluteFilePath();
    item["image"] = QStringList{"jpg", "jpeg", "png", "bmp", "webp", "ppm", "pgm"}.contains(suffix);
    item["wave"] = suffix == "wav";
    if (QStringList{"txt", "csv", "json", "md", "log"}.contains(suffix)) {
        QFile input(file.absoluteFilePath());
        if (input.open(QIODevice::ReadOnly)) {
            auto text = QString::fromUtf8(input.read(16384));
            if (!input.atEnd())
                text += tr("\n\n… 预览已截断");
            item["text"] = text;
        }
    }
    return item;
}

bool ProjectWorkspace::playEvidenceVideo(const QString &evidenceId) const
{
    const auto file = evidenceFile(evidenceId);
    if (file.value("kind").toString() != "video")
        return false;
    const auto player = QStandardPaths::findExecutable("ffplay");
    return !player.isEmpty() && QProcess::startDetached(player, {
        "-autoexit", "-loglevel", "error", "-window_title", file.value("title").toString(),
        "-x", "840", "-y", "525", file.value("localPath").toString()
    });
}

QVariantMap ProjectWorkspace::addDevice(const QString &name)
{
    const auto label = name.trimmed();
    if (!ready() || label.isEmpty() || label.size() > 100
        || !execute(db_, "INSERT INTO devices(id,name) VALUES(?,?)", {identifier(), label}))
        return failure(tr("无法添加设备，请检查设备名称是否重复。"));
    log(tr("添加设备"), label);
    emit changed();
    return success();
}

QVariantMap ProjectWorkspace::registerAsset(const QString &assetId)
{
    if (!ready() || !AccessPolicy(business_, session_).assetWork(assetId))
        return failure(tr("请先打开工程并选择可巡检的设备。"));
    QSqlQuery asset(business_->connection());
    asset.prepare("SELECT name FROM assets WHERE id=? AND active=1");
    asset.addBindValue(assetId);
    if (!asset.exec() || !asset.next())
        return failure(tr("巡检设备不可用。"));
    const auto name = asset.value(0).toString();
    const auto existing = rows("SELECT d.id,l.asset_id FROM devices d LEFT JOIN project_device_links l ON l.device_id=d.id "
                               "WHERE d.name=? OR l.asset_id=? ORDER BY (l.asset_id=?) DESC LIMIT 1", {name, assetId, assetId});
    const auto item = existing.value(0).toMap();
    const auto bound = item.value("asset_id").toString();
    const auto deviceId = existing.isEmpty() ? identifier() : item.value("id").toString();
    if (bound == assetId)
        return {{"ok", true}, {"id", deviceId}};
    if (!bound.isEmpty() || !db_.transaction())
        return failure(tr("此工程中已有同名设备，且已绑定到其他设备档案。"));
    if ((existing.isEmpty() && !execute(db_, "INSERT INTO devices(id,name) VALUES(?,?)", {deviceId, name})) ||
        !execute(db_, "INSERT INTO project_device_links(device_id,asset_id) VALUES(?,?)", {deviceId, assetId}) ||
        !db_.commit()) {
        db_.rollback();
        return failure(tr("无法将设备登记到工程。"));
    }
    log(tr("登记巡检设备"), name);
    emit changed();
    return {{"ok", true}, {"id", deviceId}};
}

QVariantMap ProjectWorkspace::importEvidence(const QUrl &file, const QString &device,
                                              const QString &point, const QString &kind,
                                              const QString &captureTime, const QString &recordId)
{
    const QFileInfo source(file.toLocalFile());
    auto timestamp = captureTime.trimmed().isEmpty() ? source.lastModified()
                                                     : QDateTime::fromString(captureTime, Qt::ISODate);
    const auto destination = EvidenceFolders.value(kind);
    if (!ready() || destination.isEmpty() || point.trimmed().isEmpty() || point.size() > 120
        || !file.isLocalFile() || !source.isFile() || !timestamp.isValid()
        || rows("SELECT id FROM devices WHERE id=?", {device}).isEmpty())
        return failure(tr("请选择工程设备、测点和文件，时间请使用 ISO 8601 格式。"));
    if (!recordId.isEmpty()) {
        if (!AccessPolicy(business_, session_).recordEditable(recordId))
            return failure(tr("本次巡检已提交或当前账号无权添加资料。"));
        QSqlQuery record(business_->connection());
        record.prepare("SELECT asset_id FROM inspection_records WHERE id=?");
        record.addBindValue(recordId);
        if (!record.exec() || !record.next() ||
            rows("SELECT device_id FROM project_device_links WHERE device_id=? AND asset_id=?",
                 {device, record.value(0)}).isEmpty())
            return failure(tr("工程设备与本次巡检的设备不一致。"));
    }
    const QDir dir(project_.value("path").toString());
    const auto folder = QFileInfo(dir.filePath(destination)).canonicalFilePath();
    if (!folder.startsWith(dir.absolutePath() + '/'))
        return failure(tr("工程证据目录不可用。"));
    const auto id = identifier();
    const auto relative = destination + '/' + id + (source.suffix().isEmpty() ? QString() : '.' + source.suffix());
    const auto output = dir.filePath(relative);
    if (!QFile::copy(source.absoluteFilePath(), output))
        return failure(tr("证据文件复制失败。"));
    if (!db_.transaction()) {
        QFile::remove(output);
        return failure(tr("无法启动证据保存。"));
    }
    if (!execute(db_, "INSERT INTO evidence(id,device_id,point,kind,title,path,captured_ms) VALUES(?,?,?,?,?,?,?)",
                 {id, device, point.trimmed(), kind, source.fileName(), relative, timestamp.toMSecsSinceEpoch()}) ||
        (!recordId.isEmpty() && !execute(db_, "INSERT INTO project_evidence_records VALUES(?,?)", {id, recordId})) ||
        !db_.commit()) {
        db_.rollback();
        QFile::remove(output);
        return failure(tr("证据索引写入失败。"));
    }
    log(tr("导入证据"), source.fileName());
    emit changed();
    return {{"ok", true}, {"id", id}};
}

QVariantMap ProjectWorkspace::importCaptured(const QString &path, const QString &device,
                                             const QString &point, const QString &kind, const QString &recordId)
{
    return importEvidence(QUrl::fromLocalFile(path), device, point, kind, {}, recordId);
}

QVariantMap ProjectWorkspace::addTextEvidence(const QString &device, const QString &point,
                                              const QString &title, const QString &text, const QString &recordId)
{
    const auto label = title.trimmed();
    if (!ready() || !validName(label) || text.trimmed().isEmpty() || text.size() > 50000)
        return failure(tr("请输入有效的标题和文字，正文最长 50000 字。"));
    const QTemporaryDir temporary;
    const auto path = QDir(temporary.path()).filePath(label + ".txt");
    if (!temporary.isValid() || !writeFile(path, text.trimmed().toUtf8()))
        return failure(tr("无法保存文字记录。"));
    return importCaptured(path, device, point, "text", recordId);
}

QVariantMap ProjectWorkspace::evidenceGraph(const QString &device) const
{
    if (!ready())
        return {};
    QVariantList nodes, edges;
    QSet<QString> added;
    const auto node = [&](const QString &id, const QString &name, const QString &type, int layer) {
        if (!added.contains(id)) {
            nodes.append(QVariantMap{{"id", id}, {"name", name}, {"type", type}, {"layer", layer}});
            added.insert(id);
        }
    };
    const auto edge = [&](const QString &from, const QString &to, const QString &predicate) {
        edges.append(QVariantMap{{"id", from + '>' + to}, {"from", from}, {"to", to}, {"predicate", predicate}});
    };
    node("project", project_.value("name").toString(), tr("工程"), 0);
    const QMap<QString, QString> labels{{"image", tr("图像")}, {"audio", tr("声音")}, {"thermal", tr("热像")},
                                       {"video", tr("视频")}, {"signal", tr("信号")}, {"text", tr("文字")}};
    const auto latest = rows("SELECT e.*,d.name device FROM evidence e JOIN devices d ON d.id=e.device_id "
                             "WHERE (?='' OR e.device_id=?) ORDER BY e.captured_ms DESC,e.id LIMIT 36",
                             {device.isEmpty() ? QStringLiteral("") : device, device});
    for (const auto &value : latest) {
        const auto row = value.toMap();
        const auto deviceNode = "device:" + row.value("device_id").toString();
        const auto point = row.value("point").toString();
        const auto pointNode = deviceNode + "/point:" + point;
        if (!added.contains(deviceNode)) {
            node(deviceNode, row.value("device").toString(), tr("设备"), 1);
            edge("project", deviceNode, tr("包含"));
        }
        if (!added.contains(pointNode)) {
            node(pointNode, point, tr("测点"), 2);
            edge(deviceNode, pointNode, tr("测点"));
        }
        const auto evidenceNode = "evidence:" + row.value("id").toString();
        const auto kind = labels.value(row.value("kind").toString(), tr("资料"));
        node(evidenceNode, row.value("title").toString(), kind, 3);
        edge(pointNode, evidenceNode, kind);
    }
    const auto eventRows = rows("SELECT id,anchor_id,evidence_ids FROM fusion_events ORDER BY created_at DESC,id LIMIT 36");
    for (const auto &value : eventRows) {
        const auto row = value.toMap();
        const auto anchor = "evidence:" + row.value("anchor_id").toString();
        if (!added.contains(anchor))
            continue;
        const auto event = "event:" + row.value("id").toString();
        node(event, tr("融合事件"), tr("事件"), 4);
        for (const auto &id : QJsonDocument::fromJson(row.value("evidence_ids").toByteArray()).array()) {
            const auto source = "evidence:" + id.toString();
            if (added.contains(source))
                edge(source, event, tr("关联"));
        }
    }
    return {{"nodes", nodes}, {"edges", edges}, {"layout", "layers"}};
}

QVariantMap ProjectWorkspace::fuse(const QString &anchorId)
{
    const auto matches = rows("SELECT e.device_id,e.point,e.captured_ms,s.record_id FROM evidence e "
                              "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id WHERE e.id=?", {anchorId});
    if (matches.isEmpty())
        return failure(tr("请先选中一条证据。"));
    const auto anchor = matches.first().toMap();
    const auto time = anchor.value("captured_ms").toLongLong();
    const auto recordId = anchor.value("record_id").toString();
    if (!recordId.isEmpty() && !AccessPolicy(business_, session_).record(recordId))
        return failure(tr("当前账号无权查看该巡检记录。"));
    const auto candidates = rows("SELECT e.id,e.kind FROM evidence e "
                                 "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id "
                                 "WHERE e.device_id=? AND e.point=? AND e.captured_ms BETWEEN ? AND ? "
                                 "AND COALESCE(s.record_id,'')=? ORDER BY e.captured_ms,e.id",
                                 {anchor.value("device_id"), anchor.value("point"), time - 60000, time + 60000,
                                  recordId.isEmpty() ? QStringLiteral("") : recordId});
    QSet<QString> modalities;
    QJsonArray ids;
    for (const auto &candidate : candidates) {
        const auto row = candidate.toMap();
        modalities.insert(row.value("kind").toString());
        ids.append(row.value("id").toString());
    }
    if (modalities.size() < 2)
        return failure(tr("需要同一设备、同一测点在前后 60 秒内的至少两种模态证据。"));
    const auto id = identifier();
    const auto linked = QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact));
    const auto existing = rows("SELECT id FROM fusion_events WHERE anchor_id=? AND evidence_ids=?", {anchorId, linked});
    if (!existing.isEmpty())
        return {{"ok", true}, {"id", existing.first().toMap().value("id")}};
    if (!db_.transaction())
        return failure(tr("无法启动融合。"));
    if (!execute(db_, "INSERT INTO fusion_events VALUES(?,?,?,?)", {id, anchorId, linked, now()}) ||
        (!recordId.isEmpty() && !execute(db_, "INSERT INTO project_event_details(event_id,record_id) VALUES(?,?)", {id, recordId})) ||
        !db_.commit()) {
        db_.rollback();
        return failure(tr("融合事件保存失败。"));
    }
    log(tr("生成融合事件"), tr("%1 条证据，%2 种模态").arg(ids.size()).arg(modalities.size()));
    emit changed();
    return {{"ok", true}, {"id", id}};
}

QVariantMap ProjectWorkspace::analysisInput(const QString &eventId) const
{
    const auto event = rows("SELECT evidence_ids FROM fusion_events WHERE id=?", {eventId});
    if (event.isEmpty())
        return failure(tr("请先选择融合事件。"));
    const auto json = event.first().toMap().value("evidence_ids").toString().toUtf8();
    QStringList lines;
    int excerptBudget = 1800;
    for (const auto id : QJsonDocument::fromJson(json).array()) {
        const auto record = rows("SELECT e.kind,e.title,e.point,e.captured_ms,d.name FROM evidence e "
                                 "JOIN devices d ON d.id=e.device_id WHERE e.id=?", {id.toString()});
        if (!record.isEmpty()) {
            const auto r = record.first().toMap();
            lines << QStringLiteral("- [%1] %2 / %3 / %4 / %5").arg(
                r.value("kind").toString(), r.value("name").toString(), r.value("point").toString(),
                QDateTime::fromMSecsSinceEpoch(r.value("captured_ms").toLongLong(), Qt::UTC).toString(Qt::ISODate),
                r.value("title").toString().simplified());
            if (r.value("kind").toString() == "text" && excerptBudget > 0) {
                const auto excerpt = evidenceFile(id.toString()).value("text").toString()
                    .simplified().left(qMin(700, excerptBudget));
                if (!excerpt.isEmpty()) {
                    lines << QStringLiteral("  文本摘录：%1").arg(excerpt);
                    excerptBudget -= excerpt.size();
                }
            }
        }
    }
    if (lines.isEmpty())
        return failure(tr("未找到可用于分析的事件证据。"));
    const auto manual = rows("SELECT title,note FROM project_manual_events WHERE event_id=?", {eventId}).value(0).toMap();
    if (!manual.isEmpty())
        lines << tr("人工观察：%1 / %2").arg(manual.value("title").toString(), manual.value("note").toString());
    return {{"ok", true}, {"summary", lines.join('\n')}};
}

QVariantMap ProjectWorkspace::saveFinding(const QString &projectId, const QString &eventId,
                                            const QString &text, const QString &model)
{
    if (!ready() || project_.value("id") != projectId || text.trimmed().isEmpty() || !db_.transaction())
        return failure(tr("当前工程已切换，或分析结果无法保存。"));
    if (!execute(db_, "INSERT INTO ai_findings VALUES(?,?,?,?,?)", {identifier(), eventId, text.trimmed(), model, now()}) ||
        !execute(db_, "INSERT INTO project_event_details(event_id,ai_result) VALUES(?,?) "
                      "ON CONFLICT(event_id) DO UPDATE SET ai_result=excluded.ai_result", {eventId, classification(text)}) ||
        !db_.commit()) {
        db_.rollback();
        return failure(tr("无法保存诊断与事件判定。"));
    }
    log(tr("保存 AI 分析"), model);
    emit changed();
    return success();
}

QVariantList ProjectWorkspace::inspectionChoices(const QString &eventId) const
{
    const auto targets = rows("SELECT l.asset_id FROM fusion_events f JOIN evidence e ON e.id=f.anchor_id "
                              "LEFT JOIN project_device_links l ON l.device_id=e.device_id WHERE f.id=?", {eventId});
    if (targets.isEmpty())
        return {};
    auto asset = targets.first().toMap().value("asset_id").toString();
    if (asset.isEmpty()) asset = QStringLiteral("");
    const AccessPolicy access(business_, session_);
    QVariantList choices;
    QSqlQuery query(business_->connection());
    query.prepare("SELECT r.id,a.name,r.created_at,t.task_code FROM inspection_records r JOIN assets a ON a.id=r.asset_id "
                  "LEFT JOIN inspection_tasks t ON t.id=r.task_id WHERE (?='' OR r.asset_id=?) ORDER BY r.created_at DESC");
    query.addBindValue(asset); query.addBindValue(asset);
    if (query.exec())
        while (query.next() && choices.size() < 60) {
            const auto id = query.value(0).toString();
            if (access.record(id))
                choices.append(QVariantMap{{"id", id}, {"name", tr("%1 · %2 · %3").arg(query.value(1).toString(),
                    query.value(3).toString().isEmpty() ? tr("手工记录") : query.value(3).toString(),
                    query.value(2).toString().left(16).replace('T', ' '))}});
        }
    return choices;
}

QVariantMap ProjectWorkspace::linkInspection(const QString &eventId, const QString &recordId)
{
    const AccessPolicy access(business_, session_);
    if (!ready() || !access.permission("inspect") || !access.record(recordId))
        return failure(tr("请选择有权查看的巡检记录。"));
    const auto targets = rows("SELECT e.device_id,l.asset_id,m.record_id,s.record_id evidence_record FROM fusion_events f JOIN evidence e ON e.id=f.anchor_id "
                              "LEFT JOIN project_device_links l ON l.device_id=e.device_id "
                              "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id "
                              "LEFT JOIN project_event_details m ON m.event_id=f.id WHERE f.id=?", {eventId});
    if (targets.isEmpty())
        return failure(tr("当前工程中没有此事件。"));
    const auto target = targets.first().toMap();
    const auto origin = target.value("evidence_record").toString();
    if (!origin.isEmpty() && origin != recordId)
        return failure(tr("此事件使用了指定巡检的资料，请保持原巡检关联。"));
    const auto previous = target.value("record_id").toString();
    if (!previous.isEmpty() && !access.record(previous) && !access.manages())
        return failure(tr("当前账号无法更改已有巡检关联。"));
    QSqlQuery record(business_->connection());
    record.prepare("SELECT asset_id FROM inspection_records WHERE id=?");
    record.addBindValue(recordId);
    if (!record.exec() || !record.next() || record.value(0).toString().isEmpty())
        return failure(tr("巡检记录未关联设备。"));
    const auto asset = record.value(0).toString();
    const auto bound = target.value("asset_id").toString();
    if (!bound.isEmpty() && bound != asset)
        return failure(tr("巡检记录所属设备与工程设备已绑定的设备不同。"));
    if (!db_.transaction())
        return failure(tr("无法写入巡检关联。"));
    if ((bound.isEmpty() && !execute(db_, "INSERT INTO project_device_links(device_id,asset_id) VALUES(?,?)",
                                     {target.value("device_id"), asset})) ||
        (previous != recordId && !execute(db_, "DELETE FROM project_event_defects WHERE event_id=?", {eventId})) ||
        !execute(db_, "INSERT INTO project_event_details(event_id,record_id) VALUES(?,?) "
                      "ON CONFLICT(event_id) DO UPDATE SET record_id=excluded.record_id", {eventId, recordId}) ||
        !db_.commit()) {
        db_.rollback();
        return failure(tr("无法写入巡检关联。"));
    }
    log(tr("关联巡检记录"), eventId + " / " + recordId);
    emit changed();
    return success();
}

QVariantMap ProjectWorkspace::linkDefect(const QString &eventId, const QString &defectId)
{
    const AccessPolicy access(business_, session_);
    if (!ready() || !access.permission("register_defect") || !access.defect(defectId))
        return failure(tr("请选择有权操作的缺陷。"));
    QVariantMap event;
    for (const auto &entry : events())
        if (entry.toMap().value("id") == eventId) {
            event = entry.toMap();
            break;
        }
    if (event.value("classification") != "abnormal" || event.value("record_id").toString().isEmpty())
        return failure(tr("先把异常事件关联到本次巡检记录。"));
    QSqlQuery query(business_->connection());
    query.prepare("SELECT 1 FROM defects WHERE id=? AND record_id=? AND asset_id=?");
    query.addBindValue(defectId); query.addBindValue(event.value("record_id")); query.addBindValue(event.value("asset_id"));
    if (!query.exec() || !query.next())
        return failure(tr("缺陷必须属于该事件关联的设备和巡检记录。"));
    if (!execute(db_, "INSERT INTO project_event_defects VALUES(?,?) "
                      "ON CONFLICT(event_id) DO UPDATE SET defect_id=excluded.defect_id", {eventId, defectId}))
        return failure(tr("缺陷关联保存失败。"));
    log(tr("关联缺陷"), eventId + " / " + defectId);
    emit changed();
    return success();
}

QVariantMap ProjectWorkspace::reviewEvent(const QString &eventId, const QString &value)
{
    if (!ready() || !session_->can("review") ||
        (value != "normal" && value != "abnormal" && value != "unknown"))
        return failure(tr("当前账号无权判定事件。"));
    if (!execute(db_, "INSERT INTO project_event_details(event_id,review_result) VALUES(?,?) "
                      "ON CONFLICT(event_id) DO UPDATE SET review_result=excluded.review_result", {eventId, value}))
        return failure(tr("事件判定保存失败。"));
    log(tr("复核事件"), eventId + " / " + classificationLabel(value));
    emit changed();
    return success();
}

QVariantMap ProjectWorkspace::addAnalysisEvent(const QString &anchorId, const QString &source, const QString &time,
                                               const QString &title, const QString &note, const QString &value)
{
    const auto timestamp = QDateTime::fromString(time.trimmed(), Qt::ISODate);
    const auto anchor = rows("SELECT e.id,s.record_id FROM evidence e LEFT JOIN project_evidence_records s "
                             "ON s.evidence_id=e.id WHERE e.id=?", {anchorId}).value(0).toMap();
    if (!ready() || !session_->can("inspect") || title.trimmed().isEmpty() || title.size() > 100 ||
        source.trimmed().isEmpty() || source.size() > 60 || note.trimmed().isEmpty() || note.size() > 1600 ||
        !timestamp.isValid() || (value != "normal" && value != "abnormal" && value != "unknown") ||
        anchor.isEmpty())
        return failure(tr("请选择工程证据，填写标题、观察和有效的发生时间。"));
    const auto recordId = anchor.value("record_id").toString();
    if (!recordId.isEmpty() && !AccessPolicy(business_, session_).record(recordId))
        return failure(tr("当前账号无权查看该巡检记录。"));
    const auto id = identifier();
    const auto evidence = QString::fromUtf8(QJsonDocument(QJsonArray{anchorId}).toJson(QJsonDocument::Compact));
    if (!db_.transaction())
        return failure(tr("无法开始登记事件。"));
    if (!execute(db_, "INSERT INTO fusion_events VALUES(?,?,?,?)", {id, anchorId, evidence, now()}) ||
        !execute(db_, "INSERT INTO project_manual_events VALUES(?,?,?,?,?,?)",
                 {id, timestamp.toMSecsSinceEpoch(), title.trimmed(), note.trimmed(), source.trimmed(), value}) ||
        (!recordId.isEmpty() && !execute(db_, "INSERT INTO project_event_details(event_id,record_id) VALUES(?,?)", {id, recordId})) ||
        !db_.commit()) {
        db_.rollback();
        return failure(tr("人工事件登记失败。"));
    }
    log(tr("登记人工事件"), title.trimmed());
    emit changed();
    return {{"ok", true}, {"id", id}};
}

QVariantMap ProjectWorkspace::reportAnalysisInput(const QString &reportId) const
{
    const auto text = reportText(reportId);
    return text.isEmpty() ? failure(tr("报告不可访问。"))
                          : QVariantMap{{"ok", true}, {"summary", text.left(7500)}};
}

QString ProjectWorkspace::findingForEvent(const QString &eventId) const
{
    const auto result = rows("SELECT body FROM ai_findings WHERE event_id=? ORDER BY rowid DESC LIMIT 1", {eventId});
    return result.isEmpty() ? QString() : result.first().toMap().value("body").toString();
}

QVariantMap ProjectWorkspace::exportReport(const QString &eventId)
{
    const auto event = analysisInput(eventId);
    if (!event.value("ok").toBool())
        return event;
    const auto findings = rows("SELECT body,model FROM ai_findings WHERE event_id=? "
                               "ORDER BY created_at DESC,rowid DESC LIMIT 1", {eventId});
    const auto finding = findings.isEmpty() ? QVariantMap() : findings.first().toMap();
    const auto details = rows("SELECT d.record_id,x.defect_id,COALESCE(n.observed_ms,e.captured_ms) observed_ms,"
                              "COALESCE(d.review_result,NULLIF(n.classification,'unknown'),d.ai_result,'unknown') classification "
                              "FROM fusion_events f LEFT JOIN project_event_details d ON d.event_id=f.id "
                              "JOIN evidence e ON e.id=f.anchor_id "
                              "LEFT JOIN project_manual_events n ON n.event_id=f.id "
                              "LEFT JOIN project_event_defects x ON x.event_id=f.id WHERE f.id=?", {eventId}).value(0).toMap();
    const auto recordId = details.value("record_id").toString();
    const auto defectId = details.value("defect_id").toString();
    const AccessPolicy access(business_, session_);
    QString links;
    if (!recordId.isEmpty() && access.record(recordId))
        links += tr("关联巡检记录编号：%1\n\n").arg(recordId);
    if (!defectId.isEmpty() && access.defect(defectId))
        links += tr("关联缺陷编号：%1\n\n").arg(defectId);
    const auto observed = QDateTime::fromMSecsSinceEpoch(details.value("observed_ms").toLongLong(), Qt::UTC)
        .toString(Qt::ISODate);
    const QDir dir(project_.value("path").toString());
    if (!QFileInfo(dir.filePath("reports")).canonicalFilePath().startsWith(dir.absolutePath() + '/'))
        return failure(tr("工程报告目录不可用。"));
    const auto id = identifier();
    const auto relative = QStringLiteral("reports/巡检证据报告-%1.md").arg(id.left(12));
    const auto path = dir.filePath(relative);
    QString report = QStringLiteral("# 巡检证据报告\n\n工程：%1\n\n时间（UTC）：%2\n\n"
                                    "事件编号：%3\n\n事件判定：%4\n\n发生时间（UTC）：%5\n\n%6## 关联证据\n\n%7\n\n## 分析\n\n")
        .arg(project_.value("name").toString(), now(), eventId,
             classificationLabel(details.value("classification").toString()), observed, links, event.value("summary").toString());
    report += finding.isEmpty() ? tr("本报告记录证据关联，未执行 AI 分析。\n")
        : tr("模型：%1。输入为证据索引和可用的文本摘录。\n\n%2\n\n"
             "AI 内容仅供复核参考，现场结论以巡检记录和人工复核为准。\n")
            .arg(finding.value("model").toString(), finding.value("body").toString());
    if (!writeFile(path, report.toUtf8()))
        return failure(tr("报告文件写入失败。"));
    if (!execute(db_, "INSERT INTO reports VALUES(?,?,?,?)", {id, eventId, relative, now()})) {
        QFile::remove(path);
        return failure(tr("报告索引写入失败。"));
    }
    log(tr("导出报告"), relative);
    emit changed();
    return {{"ok", true}, {"id", id}, {"path", path}};
}

QUrl ProjectWorkspace::reportUrl(const QString &reportId) const
{
    const auto result = rows("SELECT path FROM reports WHERE id=?", {reportId});
    if (result.isEmpty())
        return {};
    const QDir dir(project_.value("path").toString());
    const auto relative = result.first().toMap().value("path").toString();
    return managedFile(dir, relative) ? QUrl::fromLocalFile(dir.filePath(relative)) : QUrl();
}

QString ProjectWorkspace::reportText(const QString &reportId) const
{
    const auto url = reportUrl(reportId);
    if (url.isEmpty())
        return {};
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {};
    return QString::fromUtf8(file.readAll());
}

QString ProjectWorkspace::reportHtml(const QString &reportId) const
{
    const auto markdown = reportText(reportId);
    if (markdown.isEmpty())
        return {};
    QTextDocument document;
    formatReport(document, markdown);
    return document.toHtml();
}

QVariantMap ProjectWorkspace::exportReportPdf(const QString &reportId, const QUrl &file) const
{
    const auto markdown = reportText(reportId);
    if (markdown.isEmpty() || !file.isLocalFile()
        || QFileInfo(file.toLocalFile()).suffix().compare("pdf", Qt::CaseInsensitive) != 0)
        return failure(tr("请选择有效的报告与 PDF 保存路径。"));
    QSaveFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly))
        return failure(tr("无法写入 PDF 文件。"));
    {
        QPdfWriter writer(&out);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(16, 16, 16, 16));
        writer.setResolution(96);
        writer.setTitle(tr("巡检证据报告"));
        QTextDocument document;
        formatReport(document, markdown);
        document.print(&writer);
    }
    if (out.size() <= 0 || !out.commit())
        return failure(tr("PDF 导出失败。"));
    return {{"ok", true}, {"path", file.toLocalFile()}};
}
