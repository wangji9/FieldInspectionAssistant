#include "EvidenceService.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include "../database/DatabaseManager.h"
#include "../services/CameraService.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
#include <QSaveFile>

EvidenceService::EvidenceService(DatabaseManager *db, CameraService *camera, QObject *parent)
    : QObject(parent), db_(db), camera_(camera) {
    if (camera_) {
        connect(camera_, &CameraService::opened, this, &EvidenceService::cameraStateChanged);
        connect(camera_, &CameraService::closed, this, &EvidenceService::cameraStateChanged);
    }
}

QStringList EvidenceService::cameras() const { return CameraService::availableDevices(); }

bool EvidenceService::openCamera(const QString &path) {
    if(!AccessPolicy(db_,session_).permission("inspect")||!camera_)return false;
    QString selected = path;
    if (selected.isEmpty()) selected = CameraService::availableDevices().value(0, QStringLiteral("/dev/video2"));
    const bool ok = camera_->openDevice(selected);
    if (ok) cameraPath_ = selected;
    return ok;
}

void EvidenceService::closeCamera() { if (camera_) camera_->closeDevice(); }
bool EvidenceService::cameraOpen() const { return camera_ && camera_->isOpen(); }

QVariantMap EvidenceService::capture(const QString &recordId, const QString &defectId, const QString &note) {
    QVariantMap out{{QStringLiteral("ok"), false}};
    AccessPolicy access(db_,session_);
    if(!access.permission("inspect")||(recordId.isEmpty()&&defectId.isEmpty()))return {{"ok",false},{"error",QStringLiteral("请选择可操作的巡检记录或缺陷")}};
    if(!recordId.isEmpty()&&!access.recordEditable(recordId))return {{"ok",false},{"error",QStringLiteral("记录无权修改或已锁定")}};
    if(!defectId.isEmpty()){auto d=db_->findDefect(defectId);if(!access.defect(defectId)||d.status=="closed"||(!access.manages()&&d.assigneeId!=session_->userId()&&!access.defectEditable(defectId)))return {{"ok",false},{"error",QStringLiteral("无权为该缺陷增加证据")}};if(!recordId.isEmpty()&&d.recordId!=recordId)return {{"ok",false},{"error",QStringLiteral("缺陷与巡检记录不匹配")}};}
    if (!camera_ || !camera_->isOpen()) {
        out.insert(QStringLiteral("error"), QStringLiteral("摄像头未连接，请在设置中打开摄像头后重试"));
        return out;
    }
    if (!defectId.isEmpty()) {
        // 已关闭缺陷不能无记录地修改原始证据
        const InspectionDefect d = db_->findDefect(defectId);
        if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
        if (d.status == QLatin1String("closed")) {
            out.insert(QStringLiteral("error"), QStringLiteral("缺陷已关闭，不能新增或修改原始证据"));
            return out;
        }
    }
    const QString dir = QDir(db_->evidenceDir()).filePath(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    QDir().mkpath(dir);
    const QString fileName = QStringLiteral("evd_%1_%2.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HHmmsszzz")))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString filePath = QDir(dir).filePath(fileName);
    if (!camera_->captureJpeg(filePath)) {
        out.insert(QStringLiteral("error"), QStringLiteral("拍照失败，请检查摄像头连接"));
        return out;
    }
    // 校验落盘
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1024) {
        file.remove();
        out.insert(QStringLiteral("error"), QStringLiteral("照片校验失败，已清理残留文件"));
        return out;
    }
    const QByteArray sha = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    file.close();
    QString err;
    if (!db_->createAttachment(recordId, defectId, filePath, QString::fromLatin1(sha),
                               QStringLiteral("evidence"), &err)) {
        out.insert(QStringLiteral("error"), err);
        return out;
    }
    if (!note.trimmed().isEmpty()) {
        db_->appendAudit(QStringLiteral("evidence"), QStringLiteral("record"), recordId, note.trimmed(), &err);
    }
    out[QStringLiteral("ok")] = true;
    out.insert(QStringLiteral("path"), filePath);
    out.insert(QStringLiteral("sha256"), QString::fromLatin1(sha));
    emit captured(filePath);
    return out;
}

QVariantList EvidenceService::attachments(const QString &recordId, const QString &defectId) const {
    QVariantList out;
    const auto atts = db_->loadAttachments(recordId, defectId);
    AccessPolicy access(db_,session_);
    if(!access.authenticated())return out;
    for (const AttachmentItem &a : atts) {
        if(!((!a.recordId.isEmpty()&&access.record(a.recordId))||(!a.defectId.isEmpty()&&access.defect(a.defectId))))continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), a.id);
        m.insert(QStringLiteral("recordId"), a.recordId);
        m.insert(QStringLiteral("defectId"), a.defectId);
        m.insert(QStringLiteral("path"), a.path);
        m.insert(QStringLiteral("sha256"), a.sha256);
        m.insert(QStringLiteral("kind"), a.kind);
        m.insert(QStringLiteral("createdAt"), a.createdAt);
        out.push_back(m);
    }
    return out;
}

QVariantMap EvidenceService::exportCsv(const QString &filePath,const QString &assetId) {
    QVariantMap out{{QStringLiteral("ok"), false}};
    if(!AccessPolicy(db_,session_).authenticated())return {{"ok",false},{"error",QStringLiteral("请先登录")}};
    const auto path=filePath.startsWith("file:")?QUrl(filePath).toLocalFile():filePath;
    if(!QFileInfo(path).isAbsolute())return {{"ok",false},{"error",QStringLiteral("请选择完整保存路径")}};
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))return {{"ok",false},{"error",file.errorString()}};
    QTextStream ts(&file);ts.setEncoding(QStringConverter::Utf8);ts.setGenerateByteOrderMark(true);
    auto quote=[](QString value){value.replace('"',"\"\"");return '"'+value+'"';};
    ts<<QStringLiteral("记录时间,设备编号,设备名称,模板,结果,状态,任务,说明,照片\n");
    for(const auto &r:db_->loadInspectionRecords()) {
        if(!AccessPolicy(db_,session_).record(r.id)||(!assetId.isEmpty()&&r.assetId!=assetId))continue;
        auto asset=db_->findAssetById(r.assetId);
        QStringList row{r.time,asset.code,asset.name,r.templateName,r.result,r.status=="submitted"?QStringLiteral("已提交"):QStringLiteral("草稿"),r.taskCode,r.answer,r.imagePath};
        for(auto &cell:row)cell=quote(cell);ts<<row.join(',')<<'\n';
    }
    ts.flush();if(ts.status()!=QTextStream::Ok||!file.commit())return {{"ok",false},{"error",QStringLiteral("档案保存失败")}};
    return {{"ok",true},{"path",path}};
}

QString EvidenceService::evidenceDir() const { return db_->evidenceDir(); }
