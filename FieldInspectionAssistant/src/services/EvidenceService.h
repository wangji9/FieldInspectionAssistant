#pragma once

#include <QtCore>

class DatabaseManager;
class CameraService;
class SessionManager;

// 证据服务：真实摄像头拍照、落盘校验、哈希与业务记录关联、CSV 导出。
class EvidenceService final : public QObject {
    Q_OBJECT
public:
    void setSession(SessionManager*s){session_=s;}
    explicit EvidenceService(DatabaseManager *db, CameraService *camera, QObject *parent = nullptr);

    Q_PROPERTY(bool cameraConnected READ cameraOpen NOTIFY cameraStateChanged)

    Q_INVOKABLE QStringList cameras() const;
    Q_INVOKABLE bool openCamera(const QString &path);
    Q_INVOKABLE void closeCamera();
    Q_INVOKABLE bool cameraOpen() const;
    Q_INVOKABLE QString cameraPath() const { return cameraPath_; }

    // 拍照并关联到巡检记录/缺陷（缺陷关闭后禁止新增证据）
    Q_INVOKABLE QVariantMap capture(const QString &recordId, const QString &defectId, const QString &note);
    Q_INVOKABLE QVariantList attachments(const QString &recordId = QString(), const QString &defectId = QString()) const;
    // 导出巡检记录 CSV 报表
    Q_INVOKABLE QVariantMap exportCsv(const QString &filePath,const QString &assetId=QString());
    Q_INVOKABLE QString evidenceDir() const;

signals:
    void captured(const QString &path);
    void errorOccurred(const QString &message);
    void cameraStateChanged();

private:
    SessionManager *session_=nullptr;
    DatabaseManager *db_ = nullptr;
    CameraService *camera_ = nullptr;
    QString cameraPath_;
};
