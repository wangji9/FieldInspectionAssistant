#pragma once

#include <QtCore>

class DatabaseManager;
class SessionManager;

// 设备台账服务：设备查询/建档、二维码扫码定位、模板阈值维护。
class AssetService final : public QObject {
    Q_OBJECT
public:
    void setSession(SessionManager*s){session_=s;}
    explicit AssetService(DatabaseManager *db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList workAssets()const;
    Q_INVOKABLE QVariantList assets(const QString &keyword = QString()) const;
    // 扫码/输入设备编号后直接定位设备
    Q_INVOKABLE QVariantMap findByCode(const QString &code) const;
    Q_INVOKABLE QVariantMap createAsset(const QString &code, const QString &name,
                                        const QString &location, const QString &model);
    Q_INVOKABLE QVariantMap updateAsset(const QString &id, const QString &name,
                                        const QString &location, const QString &model);
    Q_INVOKABLE QVariantList history(const QString &assetId) const;
    Q_INVOKABLE QVariantMap deleteAsset(const QString &id);
    Q_INVOKABLE QVariantList templates() const;
    Q_INVOKABLE QVariantMap updateThreshold(const QString &templateId, const QString &itemKey,
                                            const QString &minText, const QString &maxText,
                                            const QString &unit);

signals:
    void assetsChanged();

private:
    DatabaseManager *db_ = nullptr;
    SessionManager *session_=nullptr;
};
