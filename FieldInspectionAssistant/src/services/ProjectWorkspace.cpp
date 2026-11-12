#include "ProjectWorkspace.h"
#include "ProjectEvidenceMetrics.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include "../database/DatabaseManager.h"

#include <QAbstractTextDocumentLayout>
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
#include <QPainter>
#include <QSettings>
#include <QSet>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <algorithm>

namespace {
const QString Format = QStringLiteral("miiep.project");
const QMap<QString, QString> EvidenceFolders{
    {"image", "assets/images"}, {"audio", "assets/audio"}, {"thermal", "assets/thermal"},
    {"video", "assets/video"}, {"signal", "signals/raw"}, {"text", "assets/text"}};

const QMap<QString, QString> ModalityLabels{
    {"image", "图像"}, {"audio", "声音"}, {"thermal", "热成像"},
    {"video", "视频"}, {"signal", "信号"}, {"text", "文字"}};

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

QString observationLabel(const QVariantMap &entry)
{
    const auto metrics = entry.value("metrics").toMap();
    const auto observation = entry.value("observation").toMap();
    const auto source = metrics.value("source").toString();
    const auto number = [&](const char *key, int decimals = 2) {
        return QString::number(metrics.value(key).toDouble(), 'f', decimals);
    };
    if (source == "temperature_matrix")
        return QObject::tr("温度矩阵 %1 点，最低 %2 °C，最高 %3 °C，平均 %4 °C")
            .arg(metrics.value("sample_count").toString(),number("minimum"),number("maximum"),number("mean"));
    if (source == "image_pixels") {
        const auto image = QObject::tr("%1 × %2 像素，平均亮度 %3 / 255")
            .arg(metrics.value("width").toString(),metrics.value("height").toString(),number("mean_luma_255",1));
        if (entry.value("kind") == "thermal")
            return image + QObject::tr("（无温度矩阵）");
        const auto description = observation.value("description").toString();
        return description.isEmpty() ? image : image + QObject::tr("。模型观察：%1").arg(description);
    }
    if (source == "wav_pcm16")
        return QObject::tr("PCM 录音 %1 秒，取前 %2 秒，幅值 RMS %3 FS，峰值 %4 FS")
            .arg(number("duration_s"),number("window_s"),number("rms",4),number("peak",4));
    if (source == "recording_catalog")
        return QObject::tr("%1 声道，采样率 %2 Hz，录音时长 %3 秒")
            .arg(metrics.value("channels").toString(),metrics.value("sample_rate_hz").toString(),number("duration_s"));
    if (source == "timed_csv")
        return QObject::tr("时序样本 %1 点，跨度 %2 秒，RMS %3，峰值 %4（单位见原始资料）")
            .arg(metrics.value("sample_count").toString(),number("window_s"),number("rms",4),number("peak",4));
    if (entry.value("kind") == "text")
        return entry.value("excerpt").toString().left(360);
    if (observation.value("state") == "semantic")
        return QObject::tr("视频 %1 秒，%2–%3 秒采样帧，模型观察：%4")
            .arg(observation.value("duration_s").toDouble(),0,'f',2)
            .arg(observation.value("start_ms").toDouble()/1000.0,0,'f',3)
            .arg(observation.value("end_ms").toDouble()/1000.0,0,'f',3)
            .arg(observation.value("description").toString());
    if (observation.value("state") == "sampled_frames")
        return QObject::tr("视频 %1 秒，已抽取 3 帧并测量亮度变化。未获得语义描述。")
            .arg(observation.value("duration_s").toDouble(),0,'f',2);
    return QObject::tr("原始文件已归档");
}

QString evidenceReportHtml(const QVariantMap &snapshot, const QString &templateId)
{
    const bool followUp = templateId == "followup";
    const bool brief = templateId == "brief";
    const auto title = followUp ? QObject::tr("缺陷整改跟踪报告")
        : brief ? QObject::tr("巡检事件摘要") : QObject::tr("多模态巡检证据报告");
    const auto value = [&](const char *key) { return snapshot.value(key).toString().toHtmlEscaped(); };
    const auto heading = [](const QString &text) {
        return QStringLiteral("<h2 style='color:#234660;font-size:13pt;margin-top:15px'>%1</h2>").arg(text);
    };
    QString html = QStringLiteral(
        "<html><head><meta charset='utf-8'></head><body style='color:#22313b;font-size:10pt'>"
        "<p style='color:#526a7d;font-size:9pt'>现场巡检助手　｜　工程报告</p>"
        "<h1 style='color:#17344c;font-size:21pt;margin-bottom:6px'>%1</h1>"
        "<p style='color:#526a7d'>报告编号 %2　·　归档时间 %3（UTC）</p>"
        "<table width='100%' cellpadding='6' cellspacing='0' bgcolor='#eef3f6'>"
        "<tr><td width='18%'><b>工程</b></td><td>%4</td><td width='18%'><b>设备</b></td><td>%5</td></tr>"
        "<tr><td><b>测点</b></td><td>%6</td><td><b>事件判定</b></td><td>%7</td></tr>"
        "<tr><td><b>基准时间</b></td><td colspan='3'>%8（UTC）</td></tr></table>")
        .arg(title,value("reportId"),value("created"),value("project"),value("device"),
             value("point"),value("classification"),value("observed"));
    const auto rectification = [&] {
        const auto record = value("record");
        const auto defect = value("defect");
        QString block = heading(QObject::tr("整改与复检"));
        block += QStringLiteral("<p>巡检记录　%1</p><p>整改项　%2</p>")
            .arg(record.isEmpty() ? QObject::tr("未关联") : record,
                 defect.isEmpty() ? QObject::tr("未关联") : defect);
        if (!value("defectStatus").isEmpty())
            block += QStringLiteral("<p>归档时状态　%1</p>").arg(value("defectStatus"));
        return block;
    };
    const auto timeline = snapshot.value("timeline").toList();
    auto finding = snapshot.value("finding").toString().trimmed();
    finding.remove(QStringLiteral("**"));
    finding.remove(QLatin1Char(0x60));
    finding.replace(QStringLiteral("mean_luma_255"), QObject::tr("平均亮度"));
    for (qsizetype i = 0; i < timeline.size(); ++i) {
        const auto evidenceId = timeline[i].toMap().value("id").toString();
        if (!evidenceId.isEmpty())
            finding.replace(evidenceId, QObject::tr("证据 %1").arg(i+1, 2, 10, QLatin1Char('0')));
    }
    finding.remove(QRegularExpression(QStringLiteral("^\\s*判定\\s*[:：].*?(?:\\r?\\n|$)")));
    finding.replace(QRegularExpression(QStringLiteral("(?m)^#{1,6}\\s*")), QString());
    finding.replace(QRegularExpression(QStringLiteral("(?m)^\\s*[-*]\\s+")), QStringLiteral("• "));
    if (finding.size() > 260) {
        const auto end = finding.lastIndexOf(QRegularExpression(QStringLiteral("[。！？]")));
        if (end >= 100)
            finding.truncate(end + 1);
    }
    html += heading(QObject::tr("事件结论"));
    html += QStringLiteral("<p>判定依据　%1</p>").arg(value("basis"));
    if (brief)
        html += QObject::tr("<p>本事件归档 %1 条对齐证据。完整观测与模型建议见多模态证据报告。</p>").arg(timeline.size());
    else if (finding.isEmpty())
        html += QObject::tr("<p>已归档关联证据与读数。暂无模型分析建议。</p>");
    else
        html += QStringLiteral("<p>模型建议（%1 · 待核验）</p><p>%2</p>")
            .arg(value("model"), finding.toHtmlEscaped().replace('\n',"<br>"));
    const auto readings = snapshot.value("readings").toList();
    if (!readings.isEmpty() && !brief) {
        html += heading(QObject::tr("巡检读数"));
        html += QStringLiteral("<table width='100%' cellpadding='4' cellspacing='0' border='1' style='border-color:#c8d4dc'>"
                               "<thead><tr bgcolor='#dce7ef'><th>检查项</th><th>记录值</th><th>标准</th><th>结果</th></tr></thead>");
        for (const auto &value : readings) {
            const auto item = value.toMap();
            const auto label = [&](const char *key) { return item.value(key).toString().toHtmlEscaped(); };
            html += QStringLiteral("<tr><td>%1</td><td>%2 %3</td><td>%4</td><td>%5</td></tr>")
                .arg(label("name"),label("value"),label("unit"),label("threshold"),label("result"));
        }
        html += "</table>";
    }
    if (followUp || brief)
        html += rectification();
    html += heading(QObject::tr("对齐证据"));
    html += QObject::tr("<p>时间以事件基准为零点。时间偏移用于关联检索，文件时间仅供参考。</p>");
    html += QStringLiteral("<table width='100%' cellspacing='0' cellpadding='5' border='1' style='border-color:#c8d4dc'>"
                           "<thead><tr bgcolor='#dce7ef'><th width='16%'>模态</th><th width='32%'>相对时间 / UTC</th>"
                           "<th width='52%'>%1</th></tr></thead>").arg(brief ? QObject::tr("来源") : QObject::tr("证据与观测"));
    int index = 0;
    for (const auto &value : timeline) {
        const auto item = value.toMap();
        const auto label = [&](const char *key) { return item.value(key).toString().toHtmlEscaped(); };
        ++index;
        html += QStringLiteral("<tr bgcolor='%1'><td>%2</td><td>%3 <span style='color:#536b7e'>%5</span><br>%4</td>"
                               "<td><b>%6</b>%7</td></tr>")
            .arg(index % 2 ? "#ffffff" : "#f3f6f8",
                 QStringLiteral("%1  %2").arg(index, 2, 10, QLatin1Char('0')).arg(label("modality")),
                 label("offsetLabel"), label("timeUtc").replace("T"," ").remove("Z"),
                 brief ? QString() : label("clockLabel"), label("title"),
                 brief ? QString() : "<br>" + observationLabel(item).toHtmlEscaped());
    }
    html += "</table>";
    if (!followUp && !brief)
        html += rectification();
    html += "</body></html>";
    return html;
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

QString readingVerdict(const QSqlDatabase &database,const QString &recordId,const QString &point)
{
    if (recordId.isEmpty())
        return {};
    QSqlQuery readings(database);
    readings.prepare("SELECT e.metadata FROM product_evidence e JOIN measurement_points p ON p.id=e.point_id "
                     "WHERE e.record_id=? AND p.name=? AND e.kind='reading' AND e.deleted_at=''");
    readings.addBindValue(recordId);
    readings.addBindValue(point);
    if (!readings.exec())
        return {};
    bool comparable=false, incomplete=false;
    while (readings.next()) {
        const auto row=QJsonDocument::fromJson(readings.value(0).toByteArray()).object();
        const auto result=row.value("assessment").toObject();
        if (result.value("baseline_id").toString().isEmpty()) {
            incomplete=true;
            continue;
        }
        const auto state=result.value("state").toString();
        if (state=="abnormal")
            return QStringLiteral("abnormal");
        if (state=="within_baseline")
            comparable=true;
        else
            incomplete=true;
    }
    if (!comparable || incomplete)
        return {};
    QSqlQuery checks(database);
    checks.prepare("SELECT COUNT(*) FROM inspection_check_items WHERE record_id=? AND is_abnormal=1");
    checks.addBindValue(recordId);
    return checks.exec() && checks.next() && checks.value(0).toInt()==0 ? QStringLiteral("normal") : QString();
}

bool storeRule(QSqlDatabase database,const QString &eventId,const QString &verdict)
{
    if (verdict.isEmpty())
        return execute(database,"DELETE FROM project_event_rules WHERE event_id=?",{eventId});
    return execute(database,"INSERT INTO project_event_rules VALUES(?,?) "
                            "ON CONFLICT(event_id) DO UPDATE SET classification=excluded.classification",
                   {eventId,verdict});
}
}

ProjectWorkspace::ProjectWorkspace(DatabaseManager *business, SessionManager *session, QObject *parent)
    : QObject(parent), business_(business), session_(session)
{
    connect(session_, &SessionManager::sessionEnding, this, &ProjectWorkspace::closeProject);
    connect(session_, &SessionManager::sessionChanged, this, &ProjectWorkspace::changed);
}

ProjectWorkspace::~ProjectWorkspace() { release(); }
bool ProjectWorkspace::ready() const { return db_.isOpen() && AccessPolicy(business_,session_).authenticated(); }
bool ProjectWorkspace::canReadLinkedRecord(const QString &recordId) const
{
    if (recordId.isEmpty())
        return true;
    const AccessPolicy access(business_,session_);
    if (access.record(recordId))
        return true;
    // A manager can inspect a transferred project whose original business records are absent.
    if (!access.manages())
        return false;
    QSqlQuery query(business_->connection());
    query.prepare("SELECT 1 FROM inspection_records WHERE id=?");
    query.addBindValue(recordId);
    return query.exec() && !query.next();
}

bool ProjectWorkspace::canReadEvent(const QString &eventId) const
{
    const auto result=rows("SELECT d.record_id,s.record_id AS source_record FROM fusion_events f "
                           "LEFT JOIN project_event_details d ON d.event_id=f.id "
                           "LEFT JOIN project_evidence_records s ON s.evidence_id=f.anchor_id WHERE f.id=?", {eventId});
    if (result.isEmpty())
        return false;
    const auto row=result.first().toMap();
    return canReadLinkedRecord(row.value("record_id").toString()) &&
           canReadLinkedRecord(row.value("source_record").toString());
}
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
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_evidence_metrics("
                            "evidence_id TEXT PRIMARY KEY REFERENCES evidence(id),metrics TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_evidence_clock("
                            "evidence_id TEXT PRIMARY KEY REFERENCES evidence(id),source TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_event_observations("
                            "event_id TEXT PRIMARY KEY REFERENCES fusion_events(id),observations TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_report_snapshots("
                            "report_id TEXT PRIMARY KEY REFERENCES reports(id),snapshot TEXT NOT NULL)") ||
        !execute(candidate, "CREATE TABLE IF NOT EXISTS project_event_rules("
                            "event_id TEXT PRIMARY KEY REFERENCES fusion_events(id),classification TEXT NOT NULL)") ||
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
    return transferStatus();
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

QVariantMap ProjectWorkspace::transferStatus() const
{
    QStringList missing;
    const QList<QStringList> links={
        {"SELECT asset_id AS id FROM project_device_links","assets","设备"},
        {"SELECT record_id AS id FROM project_evidence_records UNION SELECT record_id FROM project_event_details","inspection_records","巡检记录"},
        {"SELECT defect_id AS id FROM project_event_defects","defects","缺陷"}
    };
    for (const auto &link:links) {
        QSqlQuery query(business_->connection());
        query.prepare("SELECT 1 FROM "+link[1]+" WHERE id=?");
        int count=0;
        for (const auto &value:rows(link[0])) {
            const auto id=value.toMap().value("id").toString();
            if (id.isEmpty())
                continue;
            query.bindValue(0,id);
            if (query.exec() && !query.next())
                ++count;
        }
        if (count)
            missing.append(tr("%1 %2 项").arg(link[2]).arg(count));
    }
    QVariantMap result=success();
    if (!missing.isEmpty())
        result["warning"]=tr("当前业务库缺少关联对象（%1）。完整移交请使用原板的业务备份。").arg(missing.join("、"));
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
    const auto scopes=rows("SELECT record_id FROM project_evidence_records UNION SELECT record_id FROM project_event_details");
    for (const auto &scope:scopes) {
        if (!canReadLinkedRecord(scope.toMap().value("record_id").toString()))
            return failure(tr("工程含有当前账号无权访问的巡检资料，请由有权人员执行完整归档或另存。"));
    }
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
        if (!visibleRecords.contains(record)) visibleRecords.insert(record, canReadLinkedRecord(record));
        if (!visibleRecords.value(record))
            entry.clear();
    }
    evidence.removeAll(QVariant());
    return evidence;
}
QVariantList ProjectWorkspace::events() const
{
    auto result = rows("SELECT f.id,f.created_at,COALESCE(n.title,e.title) title,d.name device,e.point,e.device_id,f.anchor_id, "
                       "m.record_id, l.asset_id,m.ai_result,m.review_result,r.classification rule_result,x.defect_id,"
                       "n.source,n.note,n.classification AS manual_result, COALESCE(n.observed_ms,e.captured_ms) observed_ms,"
                       "datetime(COALESCE(n.observed_ms,e.captured_ms)/1000,'unixepoch','localtime') observed,"
                       "COALESCE(m.review_result,NULLIF(n.classification,'unknown'),r.classification,'unknown') classification,"
                       "EXISTS(SELECT 1 FROM ai_findings a WHERE a.event_id=f.id) analyzed FROM fusion_events f "
                       "JOIN evidence e ON e.id=f.anchor_id JOIN devices d ON d.id=e.device_id "
                       "LEFT JOIN project_event_details m ON m.event_id=f.id "
                       "LEFT JOIN project_event_rules r ON r.event_id=f.id "
                       "LEFT JOIN project_manual_events n ON n.event_id=f.id "
                       "LEFT JOIN project_event_defects x ON x.event_id=f.id "
                       "LEFT JOIN project_device_links l ON l.device_id=e.device_id ORDER BY observed_ms DESC,f.id");
    const AccessPolicy access(business_, session_);
    for (auto &value : result) {
        auto row = value.toMap();
        if (!canReadEvent(row.value("id").toString())) {
            value.clear();
            continue;
        }
        row["classificationLabel"] = classificationLabel(row.value("classification").toString());
        const auto manual=row.value("manual_result").toString();
        if (row.value("review_result").toString().isEmpty() && (manual.isEmpty() || manual=="unknown") &&
            !row.value("rule_result").toString().isEmpty())
            row["classificationLabel"]=tr("基准%1").arg(row.value("classificationLabel").toString());
        if (!row.value("record_id").toString().isEmpty() && !access.record(row.value("record_id").toString())) {
            row.remove("record_id");
            row.remove("asset_id");
        }
        if (!row.value("defect_id").toString().isEmpty() && !access.defect(row.value("defect_id").toString()))
            row.remove("defect_id");
        value = row;
    }
    result.removeAll(QVariant());
    return result;
}
QVariantList ProjectWorkspace::reports() const {
    auto reports=rows("SELECT * FROM reports ORDER BY created_at DESC,rowid DESC");
    reports.removeIf([this](const auto &report){return !canReadEvent(report.toMap().value("event_id").toString());});
    return reports;
}
QVariantList ProjectWorkspace::logs() const
{
    return rows("SELECT action,detail,time FROM activity ORDER BY id DESC LIMIT 200");
}

QVariantMap ProjectWorkspace::evidenceFile(const QString &evidenceId) const
{
    const auto found = rows("SELECT e.title,e.path,e.kind,s.record_id FROM evidence e LEFT JOIN "
                            "project_evidence_records s ON s.evidence_id=e.id WHERE e.id=?", {evidenceId});
    if (found.isEmpty())
        return {};
    auto item = found.first().toMap();
    if (!canReadLinkedRecord(item.take("record_id").toString()))
        return {};
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
    const auto clock = captureTime.trimmed().isEmpty() ? "file_time"
        : timestamp.timeSpec() == Qt::LocalTime ? "declared_local" : "declared_offset";
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
    const auto catalog=QFileInfo(business_->databasePath()).absoluteDir().filePath("measurements/measurements.sqlite");
    const auto metrics=projectEvidenceMetrics(kind,output,source.absoluteFilePath(),catalog);
    if (!db_.transaction()) {
        QFile::remove(output);
        return failure(tr("无法启动证据保存。"));
    }
    if (!execute(db_, "INSERT INTO evidence(id,device_id,point,kind,title,path,captured_ms) VALUES(?,?,?,?,?,?,?)",
                 {id, device, point.trimmed(), kind, source.fileName(), relative, timestamp.toMSecsSinceEpoch()}) ||
        !execute(db_, "INSERT INTO project_evidence_clock VALUES(?,?)", {id, clock}) ||
        (!recordId.isEmpty() && !execute(db_, "INSERT INTO project_evidence_records VALUES(?,?)", {id, recordId})) ||
        (!metrics.isEmpty() && !execute(db_, "INSERT INTO project_evidence_metrics VALUES(?,?)",
              {id,QString::fromUtf8(QJsonDocument::fromVariant(metrics).toJson(QJsonDocument::Compact))})) ||
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

QVariantList ProjectWorkspace::eventTimeline(const QString &eventId) const
{
    if (!canReadEvent(eventId))
        return {};
    const auto event = rows("SELECT f.evidence_ids,COALESCE(n.observed_ms,a.captured_ms) reference_ms "
                            "FROM fusion_events f JOIN evidence a ON a.id=f.anchor_id "
                            "LEFT JOIN project_manual_events n ON n.event_id=f.id WHERE f.id=?", {eventId}).value(0).toMap();
    const auto observations = QJsonDocument::fromJson(rows("SELECT observations FROM project_event_observations "
        "WHERE event_id=?", {eventId}).value(0).toMap().value("observations").toByteArray()).toVariant().toList();
    QVariantList timeline;
    for (const auto &id : QJsonDocument::fromJson(event.value("evidence_ids").toByteArray()).array()) {
        auto evidence = rows("SELECT e.id,e.kind,e.title,e.captured_ms,e.point,e.device_id,d.name device,s.record_id,"
                             "m.metrics,c.source FROM evidence e JOIN devices d ON d.id=e.device_id "
                             "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id "
                             "LEFT JOIN project_evidence_metrics m ON m.evidence_id=e.id "
                             "LEFT JOIN project_evidence_clock c ON c.evidence_id=e.id WHERE e.id=?",
                             {id.toString()}).value(0).toMap();
        if (evidence.isEmpty() || !canReadLinkedRecord(evidence.take("record_id").toString()))
            continue;
        const auto time = evidence.value("captured_ms").toLongLong();
        const auto offset = time - event.value("reference_ms").toLongLong();
        const auto source = evidence.value("source").toString();
        evidence["timeUtc"] = QDateTime::fromMSecsSinceEpoch(time, Qt::UTC).toString(Qt::ISODateWithMs);
        evidence["offsetMs"] = offset;
        evidence["offsetLabel"] = (offset > 0 ? QStringLiteral("+") : QString()) + QString::number(offset) + " ms";
        evidence["clockLabel"] = source == "declared_offset" ? tr("注明时区") :
            source == "declared_local" ? tr("按本机时区解释") :
            source == "file_time" ? tr("文件时间估算") : tr("历史记录");
        evidence["modality"] = ModalityLabels.value(evidence.value("kind").toString(), tr("资料"));
        evidence["metrics"] = QJsonDocument::fromJson(evidence.value("metrics").toByteArray()).toVariant().toMap();
        for (const auto &observation : observations) {
            if (observation.toMap().value("id").toString() == id.toString()) {
                evidence["observation"] = observation;
                break;
            }
        }
        if (evidence.value("kind") == "text")
            evidence["excerpt"] = evidenceFile(id.toString()).value("text").toString().left(700);
        evidence["observationLabel"] = observationLabel(evidence).simplified();
        timeline.append(evidence);
    }
    std::stable_sort(timeline.begin(), timeline.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("captured_ms").toLongLong() < b.toMap().value("captured_ms").toLongLong();
    });
    return timeline;
}

QVariantList ProjectWorkspace::eventMedia(const QString &eventId) const
{
    QVariantList media;
    QSet<QString> kinds;
    for (const auto &value : eventTimeline(eventId)) {
        auto row = value.toMap();
        const auto kind = row.value("kind").toString();
        if ((kind != "video" && kind != "image") || kinds.contains(kind))
            continue;
        const auto path = evidenceFile(row.value("id").toString()).value("localPath");
        if (path.toString().isEmpty())
            continue;
        row["path"] = path;
        media.append(row);
        kinds.insert(kind);
    }
    return media;
}

bool ProjectWorkspace::saveObservations(const QString &projectId, const QString &eventId,
                                        const QVariantList &observations)
{
    if (project_.value("id") != projectId || !canReadEvent(eventId))
        return false;
    const auto json = QJsonDocument::fromVariant(observations).toJson(QJsonDocument::Compact);
    if (json.size() > 32768 || !execute(db_, "INSERT INTO project_event_observations VALUES(?,?) "
                                           "ON CONFLICT(event_id) DO UPDATE SET observations=excluded.observations",
                                       {eventId, QString::fromUtf8(json)}))
        return false;
    emit changed();
    return true;
}

QVariantMap ProjectWorkspace::evidenceGraph(const QString &device, const QString &eventId) const
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
    QVariantList latest;
    if (eventId.isEmpty()) {
        latest = rows("SELECT e.*,d.name device,s.record_id FROM evidence e JOIN devices d ON d.id=e.device_id "
                      "LEFT JOIN project_evidence_records s ON s.evidence_id=e.id "
                      "WHERE (?='' OR e.device_id=?) ORDER BY e.captured_ms DESC,e.id LIMIT 36",
                      {device.isEmpty() ? QStringLiteral("") : device, device});
    } else {
        latest = eventTimeline(eventId);
    }
    for (const auto &value : latest) {
        const auto row = value.toMap();
        if (!canReadLinkedRecord(row.value("record_id").toString()))
            continue;
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
        const auto kind = ModalityLabels.value(row.value("kind").toString(), tr("资料"));
        node(evidenceNode, row.value("title").toString(), kind, 3);
        edge(pointNode, evidenceNode, kind);
        auto entry = nodes.last().toMap();
        entry["captured_ms"] = row.value("captured_ms");
        if (row.contains("observationLabel"))
            entry["detail"] = row.value("timeUtc").toString() + "　" +
                row.value("offsetLabel").toString() + "　" + row.value("observationLabel").toString();
        nodes.last() = entry;
    }
    const auto eventRows = rows("SELECT id,anchor_id FROM fusion_events WHERE (?='' OR id=?) "
                                "ORDER BY created_at DESC,id LIMIT 36",
                                {eventId.isEmpty() ? QStringLiteral("") : eventId, eventId});
    for (const auto &value : eventRows) {
        const auto row = value.toMap();
        if (!canReadEvent(row.value("id").toString()))
            continue;
        const auto anchor = "evidence:" + row.value("anchor_id").toString();
        if (!added.contains(anchor))
            continue;
        const auto id = row.value("id").toString();
        const auto event = "event:" + id;
        node(event, tr("融合事件 · %1").arg(id.left(8)), tr("事件"), 4);
        for (const auto &entry : eventTimeline(id)) {
            const auto item = entry.toMap();
            const auto source = "evidence:" + item.value("id").toString();
            if (added.contains(source))
                edge(source, event, item.value("offsetLabel").toString());
        }
        const auto relations = rows("SELECT d.record_id,x.defect_id FROM fusion_events f "
                                    "LEFT JOIN project_event_details d ON d.event_id=f.id "
                                    "LEFT JOIN project_event_defects x ON x.event_id=f.id WHERE f.id=?",
                                    {id}).value(0).toMap();
        const auto record = relations.value("record_id").toString();
        const auto defect = relations.value("defect_id").toString();
        const AccessPolicy access(business_, session_);
        if (!record.isEmpty() && access.record(record)) {
            node("record:" + record, tr("巡检记录 · %1").arg(record.left(8)), tr("巡检"), 5);
            edge(event, "record:" + record, tr("巡检关联"));
        }
        if (!defect.isEmpty() && access.defect(defect)) {
            node("defect:" + defect, tr("缺陷整改 · %1").arg(defect.left(8)), tr("缺陷"), 5);
            edge(event, "defect:" + defect, tr("整改跟踪"));
        }
        const auto finding = rows("SELECT model FROM ai_findings WHERE event_id=? ORDER BY rowid DESC LIMIT 1",
                                  {id}).value(0).toMap();
        if (!finding.isEmpty()) {
            node("finding:" + id, finding.value("model").toString(), tr("诊断"), 5);
            edge(event, "finding:" + id, tr("诊断"));
        }
        for (const auto &value : rows("SELECT id FROM reports WHERE event_id=? ORDER BY rowid DESC LIMIT 5", {id})) {
            const auto report = value.toMap().value("id").toString();
            node("report:" + report, tr("巡检证据报告 · %1").arg(report.left(8)), tr("报告"), 6);
            edge(event, "report:" + report, tr("归档"));
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
        !storeRule(db_,id,readingVerdict(business_->connection(),recordId,anchor.value("point").toString())) ||
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
    if (!canReadEvent(eventId))
        return failure(tr("当前账号无权查看事件，或事件已不存在。"));
    const auto event = rows("SELECT f.evidence_ids,d.record_id FROM fusion_events f LEFT JOIN "
                            "project_event_details d ON d.event_id=f.id WHERE f.id=?", {eventId});
    if (event.isEmpty())
        return failure(tr("请先选择融合事件。"));
    QStringList lines;
    int excerptBudget = 1800;
    for (const auto &value : eventTimeline(eventId)) {
        const auto item = value.toMap();
        const auto id = item.value("id").toString();
        lines << QStringLiteral("- [%1] %2 / %3 / %4 / %5 / %6 / %7 / %8").arg(
            id, item.value("modality").toString(), item.value("device").toString(), item.value("point").toString(),
            item.value("timeUtc").toString(), item.value("offsetLabel").toString(),
            item.value("clockLabel").toString(), item.value("title").toString().simplified());
        for (const auto *field : {"metrics", "observation"}) {
            const auto data = item.value(field).toMap();
            if (!data.isEmpty())
                lines << QStringLiteral("  %1：%2").arg(QLatin1String(field) == QLatin1String("metrics") ? tr("采集指标") : tr("帧观测"),
                    QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact)));
        }
        if (item.value("kind") == "text" && excerptBudget > 0) {
            const auto excerpt = item.value("excerpt").toString().simplified().left(excerptBudget);
            if (!excerpt.isEmpty()) {
                lines << QStringLiteral("  文本摘录：%1").arg(excerpt);
                excerptBudget -= excerpt.size();
            }
        }
    }
    if (lines.isEmpty())
        return failure(tr("未找到可用于分析的事件证据。"));
    const auto manual = rows("SELECT title,note FROM project_manual_events WHERE event_id=?", {eventId}).value(0).toMap();
    if (!manual.isEmpty())
        lines << tr("人工观察：%1 / %2").arg(manual.value("title").toString(), manual.value("note").toString());
    const auto recordId=event.first().toMap().value("record_id").toString();
    if (!recordId.isEmpty() && AccessPolicy(business_,session_).record(recordId)) {
        QSqlQuery checks(business_->connection());
        checks.prepare("SELECT item_key,item_name,value_text,unit,threshold_text,result FROM inspection_check_items "
                       "WHERE record_id=? AND result<>'' ORDER BY item_key LIMIT 24");
        checks.addBindValue(recordId);
        if (checks.exec()) {
            while (checks.next())
                lines << tr("巡检项 [%1] %2，读数 %3 %4，标准 %5，结果 %6").arg(
                    checks.value(0).toString(),checks.value(1).toString().left(100),
                    checks.value(2).toString().left(60),checks.value(3).toString().left(20),
                    checks.value(4).toString().left(100),checks.value(5).toString().left(60));
        }
        QSqlQuery readings(business_->connection());
        readings.prepare("SELECT id,title,metadata FROM product_evidence WHERE record_id=? AND kind='reading' "
                         "AND deleted_at='' ORDER BY created_at DESC LIMIT 12");
        readings.addBindValue(recordId);
        if (readings.exec()) {
            while (readings.next()) {
                const auto raw=QJsonDocument::fromJson(readings.value(2).toByteArray()).toVariant().toMap();
                QVariantMap metric;
                for (const auto *key:{"metric","value","unit","operating_condition","assessment"})
                    metric.insert(key,raw.value(key));
                lines << tr("巡检读数 [%1] %2：%3").arg(readings.value(0).toString(),
                    readings.value(1).toString().left(100),QString::fromUtf8(QJsonDocument::fromVariant(metric).toJson(QJsonDocument::Compact)));
            }
        }
    }
    return {{"ok", true}, {"summary", lines.join('\n')}};
}

QVariantMap ProjectWorkspace::saveFinding(const QString &projectId, const QString &eventId,
                                            const QString &text, const QString &model)
{
    if (project_.value("id") != projectId || text.trimmed().isEmpty() || !canReadEvent(eventId) || !db_.transaction())
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
    if (!canReadEvent(eventId) || !access.permission("inspect") || !access.record(recordId))
        return failure(tr("请选择有权查看的巡检记录。"));
    const auto targets = rows("SELECT e.device_id,e.point,l.asset_id,m.record_id,s.record_id evidence_record FROM fusion_events f JOIN evidence e ON e.id=f.anchor_id "
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
        !storeRule(db_,eventId,readingVerdict(business_->connection(),recordId,target.value("point").toString())) ||
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
    if (!canReadEvent(eventId) || !session_->can("review") ||
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
    if (!canReadEvent(eventId))
        return {};
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
                              "COALESCE(d.review_result,NULLIF(n.classification,'unknown'),r.classification,'unknown') classification "
                              "FROM fusion_events f LEFT JOIN project_event_details d ON d.event_id=f.id "
                              "LEFT JOIN project_event_rules r ON r.event_id=f.id "
                              "JOIN evidence e ON e.id=f.anchor_id "
                              "LEFT JOIN project_manual_events n ON n.event_id=f.id "
                              "LEFT JOIN project_event_defects x ON x.event_id=f.id WHERE f.id=?", {eventId}).value(0).toMap();
    const auto recordId = details.value("record_id").toString();
    const auto defectId = details.value("defect_id").toString();
    const AccessPolicy access(business_, session_);
    const auto observed = QDateTime::fromMSecsSinceEpoch(details.value("observed_ms").toLongLong(), Qt::UTC)
        .toString(Qt::ISODateWithMs);
    const QDir dir(project_.value("path").toString());
    if (!QFileInfo(dir.filePath("reports")).canonicalFilePath().startsWith(dir.absolutePath() + '/'))
        return failure(tr("工程报告目录不可用。"));
    const auto id = identifier();
    const auto relative = QStringLiteral("reports/巡检证据报告-%1.md").arg(id.left(12));
    const auto path = dir.filePath(relative);
    const auto timeline = eventTimeline(eventId);
    const auto first = timeline.value(0).toMap();
    const auto created = now();
    QString defectTitle = defectId, defectStatus;
    if (!defectId.isEmpty() && access.defect(defectId)) {
        QSqlQuery defect(business_->connection());
        defect.prepare("SELECT title,status FROM defects WHERE id=?");
        defect.addBindValue(defectId);
        if (defect.exec() && defect.next()) {
            defectTitle = defect.value(0).toString();
            const QMap<QString,QString> labels{{"unassigned",tr("待分派")},{"pending",tr("待整改")},
                {"in_progress",tr("整改中")},{"review",tr("待复核")},{"closed",tr("已关闭")},{"reopened",tr("退回整改")}};
            defectStatus = labels.value(defect.value(1).toString(), tr("状态未记录"));
        }
    }
    const auto basis = rows("SELECT d.review_result,n.classification manual,r.classification rule FROM fusion_events f "
                            "LEFT JOIN project_event_details d ON d.event_id=f.id "
                            "LEFT JOIN project_manual_events n ON n.event_id=f.id "
                            "LEFT JOIN project_event_rules r ON r.event_id=f.id WHERE f.id=?", {eventId}).value(0).toMap();
    const auto valid = [&](const char *key) {
        const auto v = basis.value(key).toString();
        return v == "normal" || v == "abnormal";
    };
    const auto rationale = !basis.value("review_result").toString().isEmpty() ? tr("人工复核") :
        valid("manual") ? tr("现场登记") : valid("rule") ? tr("巡检读数与判定规则") : tr("现有证据不足");
    QVariantList readings;
    if (!recordId.isEmpty() && access.record(recordId)) {
        QMap<QPair<QString, QString>, QString> baselineLimits;
        QSqlQuery observedReadings(business_->connection());
        observedReadings.prepare("SELECT e.metadata FROM product_evidence e JOIN measurement_points p ON p.id=e.point_id "
                                 "WHERE e.record_id=? AND p.name=? AND e.kind='reading' AND e.deleted_at='' ORDER BY e.rowid");
        observedReadings.addBindValue(recordId);
        observedReadings.addBindValue(first.value("point"));
        if (observedReadings.exec()) {
            while (observedReadings.next()) {
                const auto data = QJsonDocument::fromJson(observedReadings.value(0).toByteArray()).toVariant().toMap();
                const auto id = data.value("assessment").toMap().value("baseline_id").toString();
                if (id.isEmpty())
                    continue;
                QSqlQuery baseline(business_->connection());
                baseline.prepare("SELECT minimum,maximum FROM health_baselines WHERE id=?");
                baseline.addBindValue(id);
                if (!baseline.exec() || !baseline.next())
                    continue;
                const auto low = baseline.value(0), high = baseline.value(1);
                const auto range = low.isNull() ? QStringLiteral("≤ %1").arg(high.toString())
                    : high.isNull() ? QStringLiteral("≥ %1").arg(low.toString())
                    : QStringLiteral("%1 – %2").arg(low.toString(),high.toString());
                baselineLimits[{data.value("metric").toString(), data.value("value").toString()}] =
                    range + " " + data.value("unit").toString();
            }
        }
        QSqlQuery checks(business_->connection());
        checks.prepare("SELECT item_name,value_text,unit,threshold_text,result FROM inspection_check_items "
                       "WHERE record_id=? AND result<>'' ORDER BY rowid LIMIT 24");
        checks.addBindValue(recordId);
        if (checks.exec()) {
            while (checks.next()) {
                const auto standard = checks.value(3).toString().trimmed();
                readings.append(QVariantMap{{"name",checks.value(0)},{"value",checks.value(1)},
                    {"unit",checks.value(2)},{"threshold",standard.isEmpty() ?
                        baselineLimits.value({checks.value(0).toString(),checks.value(1).toString()}, tr("—")) : standard},
                    {"result",classificationLabel(checks.value(4).toString())}});
            }
        }
    }
    const QVariantMap snapshot{{"reportId",id.left(12)},{"created",created},{"project",project_.value("name")},
        {"device",first.value("device")},{"point",first.value("point")},{"observed",observed},
        {"classification",classificationLabel(details.value("classification").toString())},{"basis",rationale},
        {"record",access.record(recordId) ? recordId : QString()},
        {"defect",access.defect(defectId) ? defectTitle : QString()},{"defectStatus",defectStatus},
        {"finding",finding.value("body")},{"model",finding.value("model")},{"timeline",timeline},{"readings",readings}};
    QTextDocument archive;
    archive.setHtml(evidenceReportHtml(snapshot, "evidence"));
    if (!writeFile(path, archive.toMarkdown().toUtf8()))
        return failure(tr("报告文件写入失败。"));
    const auto json = QString::fromUtf8(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact));
    if (!db_.transaction() || !execute(db_, "INSERT INTO reports VALUES(?,?,?,?)", {id, eventId, relative, created}) ||
        !execute(db_, "INSERT INTO project_report_snapshots VALUES(?,?)", {id, json}) || !db_.commit()) {
        db_.rollback();
        QFile::remove(path);
        return failure(tr("报告索引写入失败。"));
    }
    log(tr("导出报告"), relative);
    emit changed();
    return {{"ok", true}, {"id", id}, {"path", path}};
}

QUrl ProjectWorkspace::reportUrl(const QString &reportId) const
{
    const auto result = rows("SELECT path,event_id FROM reports WHERE id=?", {reportId});
    if (result.isEmpty() || !canReadEvent(result.first().toMap().value("event_id").toString()))
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

QString ProjectWorkspace::reportHtml(const QString &reportId, const QString &templateId) const
{
    if (reportUrl(reportId).isEmpty())
        return {};
    const auto stored = rows("SELECT snapshot FROM project_report_snapshots WHERE report_id=?", {reportId});
    if (!stored.isEmpty()) {
        const auto snapshot = QJsonDocument::fromJson(stored.first().toMap().value("snapshot").toByteArray()).toVariant().toMap();
        if (!snapshot.isEmpty())
            return evidenceReportHtml(snapshot, templateId);
    }
    const auto markdown = reportText(reportId);
    if (markdown.isEmpty())
        return {};
    QTextDocument document;
    formatReport(document, markdown);
    return document.toHtml();
}

QVariantMap ProjectWorkspace::exportReportPdf(const QString &reportId, const QUrl &file,
                                               const QString &templateId) const
{
    const auto html = reportHtml(reportId, templateId);
    if (html.isEmpty() || !file.isLocalFile()
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
        writer.setTitle(templateId == "followup" ? tr("缺陷整改跟踪报告") :
                        templateId == "brief" ? tr("巡检事件摘要") : tr("多模态巡检证据报告"));
        QTextDocument document;
        auto font = QGuiApplication::font();
        font.setPointSizeF(10);
        document.setDefaultFont(font);
        document.setDocumentMargin(0);
        document.setHtml(html);
        const int width = writer.width();
        const int footerHeight = 28;
        const int bodyHeight = writer.height() - footerHeight;
        document.setPageSize(QSizeF(width, bodyHeight));
        QPainter painter;
        if (!painter.begin(&writer))
            return failure(tr("无法创建 PDF 页面。"));
        const int pages = document.pageCount();
        for (int page = 0; page < pages; ++page) {
            if (page > 0 && !writer.newPage()) {
                painter.end();
                return failure(tr("无法添加 PDF 页面。"));
            }
            const int top = page * bodyHeight;
            painter.save();
            painter.setClipRect(QRect(0, 0, width, bodyHeight));
            painter.translate(0, -top);
            QAbstractTextDocumentLayout::PaintContext context;
            context.clip = QRectF(0, top, width, bodyHeight);
            document.documentLayout()->draw(&painter, context);
            painter.restore();
            painter.setPen(QColor("#ccd7df"));
            painter.drawLine(0, bodyHeight + 5, width, bodyHeight + 5);
            painter.setPen(QColor("#526a7d"));
            painter.setFont(font);
            painter.drawText(QRect(0, bodyHeight + 6, width, footerHeight - 6),
                             Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1 / %2").arg(page+1).arg(pages));
        }
        painter.end();
    }
    if (out.size() <= 0 || !out.commit())
        return failure(tr("PDF 导出失败。"));
    return {{"ok", true}, {"path", file.toLocalFile()}};
}
