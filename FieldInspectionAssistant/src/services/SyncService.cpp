#include "SyncService.h"
#include "AccessPolicy.h"
#include "../database/DatabaseManager.h"

SyncService::SyncService(DatabaseManager *db, QObject *parent)
    : QObject(parent), db_(db) {}

void SyncService::setOnline(bool online) {
    if(!AccessPolicy(db_,session_).permission("settings")||!configured())online=false;
    if (online_ == online) return;
    online_ = online;
    emit onlineChanged();
}

int SyncService::pendingCount() const { return db_ ? db_->pendingSyncCount() : 0; }

QVariantMap SyncService::flush() {
    QVariantMap out{{QStringLiteral("ok"), false}, {QStringLiteral("sent"), 0}};
    if(!AccessPolicy(db_,session_).permission("settings"))return {{"ok",false},{"sent",0},{"error",QStringLiteral("没有同步管理权限")}};
    if(!configured())return {{"ok",false},{"sent",0},{"error",status()}};
    if (!online_) {
        out.insert(QStringLiteral("error"), QStringLiteral("当前离线，无法同步；数据已保存在本地队列"));
        return out;
    }
    out["error"]=QStringLiteral("尚未配置正式同步服务，数据保留在本地队列");
    return out;
}

QString SyncService::lastSync() const {
    return db_ ? db_->state(QStringLiteral("last_sync"), QStringLiteral("从未同步")) : QString();
}

QVariantList SyncService::outbox() const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).permission("settings"))return out;
    const auto items = db_->pendingSyncItems();
    for (const SyncItem &it : items) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), it.id);
        m.insert(QStringLiteral("entity"), it.entity);
        m.insert(QStringLiteral("entityId"), it.entityId);
        m.insert(QStringLiteral("operation"), it.operation);
        m.insert(QStringLiteral("payloadJson"), it.payloadJson);
        m.insert(QStringLiteral("attempts"), it.attempts);
        out.push_back(m);
    }
    return out;
}
