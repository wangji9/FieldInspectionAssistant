#include "AssetService.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include <cmath>
#include "../database/DatabaseManager.h"

AssetService::AssetService(DatabaseManager *db, QObject *parent)
    : QObject(parent), db_(db) {}

QVariantList AssetService::assets(const QString &keyword) const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).authenticated())return out;
    const auto all = db_->loadAssets();
    for (const InspectionAsset &a : all) {
        if (!keyword.trimmed().isEmpty()
            && !a.code.contains(keyword, Qt::CaseInsensitive)
            && !a.name.contains(keyword, Qt::CaseInsensitive)
            && !a.location.contains(keyword, Qt::CaseInsensitive)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), a.id);
        m.insert(QStringLiteral("code"), a.code);
        m.insert(QStringLiteral("name"), a.name);
        m.insert(QStringLiteral("location"), a.location);
        m.insert(QStringLiteral("model"), a.model);
        out.push_back(m);
    }
    return out;
}

QVariantList AssetService::workAssets()const{QVariantList out;for(auto a:assets())if(AccessPolicy(db_,session_).assetWork(a.toMap().value("id").toString()))out<<a;return out;}

QVariantMap AssetService::findByCode(const QString &code) const {
    QVariantMap out{{QStringLiteral("ok"), false}};
    if(!AccessPolicy(db_,session_).authenticated())return {{"ok",false},{"error",QStringLiteral("请先登录")}};
    QString err;
    const InspectionAsset a = db_->findAssetByCode(code.trimmed(), &err);
    if (a.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("未找到设备：%1").arg(code.trimmed())); return out; }
    QVariantMap m;
    m.insert(QStringLiteral("id"), a.id);
    m.insert(QStringLiteral("code"), a.code);
    m.insert(QStringLiteral("name"), a.name);
    m.insert(QStringLiteral("location"), a.location);
    m.insert(QStringLiteral("model"), a.model);
    out[QStringLiteral("ok")] = true;
    out.insert(QStringLiteral("asset"), m);
    return out;
}

QVariantMap AssetService::createAsset(const QString &code, const QString &name,
                                      const QString &location, const QString &model) {
    if(!AccessPolicy(db_,session_).permission("manage_assets"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    if (code.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        out.insert(QStringLiteral("error"), QStringLiteral("设备编号和名称不能为空")); return out;
    }
    QString err;
    const InspectionAsset existing = db_->findAssetByCode(code.trimmed(), &err);
    if (!existing.id.isEmpty()) {
        out.insert(QStringLiteral("error"), QStringLiteral("设备编号已存在")); return out;
    }
    InspectionAsset a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.code = code.trimmed();
    a.name = name.trimmed();
    a.location = location.trimmed();
    a.model = model.trimmed();
    a.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!db_->createAsset(a, &err)) { out.insert(QStringLiteral("error"), err); return out; }
    out[QStringLiteral("ok")] = true;
    out.insert(QStringLiteral("id"), a.id);
    emit assetsChanged();
    return out;
}

QVariantMap AssetService::updateAsset(const QString &id, const QString &name,
                                      const QString &location, const QString &model) {
    if(!AccessPolicy(db_,session_).permission("manage_assets"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    if(name.trimmed().isEmpty()||db_->findAssetById(id).id.isEmpty())return {{"ok",false},{"error",QStringLiteral("请选择设备并填写名称")}};
    QString err;
    if (!db_->updateAsset(id, name, location, model, &err)) { out.insert(QStringLiteral("error"), err); return out; }
    out[QStringLiteral("ok")] = true;
    emit assetsChanged();
    return out;
}

QVariantList AssetService::history(const QString &assetId) const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).authenticated())return out;
    const auto recs = db_->loadInspectionRecords();
    for (const InspectionRecord &r : recs) {
        if (r.assetId != assetId||!AccessPolicy(db_,session_).record(r.id)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), r.id);
        m.insert(QStringLiteral("time"), r.time);
        m.insert(QStringLiteral("templateName"), r.templateName);
        m.insert(QStringLiteral("result"), r.result);
        m.insert(QStringLiteral("status"), r.status);
        m.insert(QStringLiteral("statusText"), r.status == QLatin1String("submitted") ? QStringLiteral("已提交")
                                            : r.status == QLatin1String("draft") ? QStringLiteral("草稿")
                                            : r.status);
        m.insert(QStringLiteral("taskId"), r.taskId);
        out.push_back(m);
    }
    return out;
}

QVariantList AssetService::templates() const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).authenticated())return out;
    const auto tmpls = db_->loadTemplates();
    for (const TemplateInfo &t : tmpls) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), t.id);
        m.insert(QStringLiteral("name"), t.name);
        m.insert(QStringLiteral("version"), t.version);
        m.insert(QStringLiteral("createdAt"), t.createdAt);
        out.push_back(m);
    }
    return out;
}

QVariantMap AssetService::updateThreshold(const QString &templateId, const QString &itemKey,
                                          const QString &minText, const QString &maxText,
                                          const QString &unit) {
    if(!AccessPolicy(db_,session_).permission("manage_assets"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    QString err;
    if (!db_->updateTemplateThreshold(templateId, itemKey, minText, maxText, unit, &err)) {
        out.insert(QStringLiteral("error"), err); return out;
    }
    out[QStringLiteral("ok")] = true;
    return out;
}

QVariantMap AssetService::deleteAsset(const QString &id) {
    if(!AccessPolicy(db_,session_).permission("manage_assets"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};QString e;if(!db_->removeEntity("asset",id,&e))return {{"ok",false},{"error",e}};emit assetsChanged();return {{"ok",true}};}
