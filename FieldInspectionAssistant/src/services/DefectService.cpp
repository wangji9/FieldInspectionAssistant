#include "DefectService.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include <cmath>
#include "../database/DatabaseManager.h"

DefectService::DefectService(DatabaseManager *db, QObject *parent)
    : QObject(parent), db_(db) {}

QString DefectService::statusText(const QString &status) {
    if (status == QLatin1String("unassigned")) return QStringLiteral("未分派");
    if (status == QLatin1String("pending")) return QStringLiteral("待处理");
    if (status == QLatin1String("in_progress")) return QStringLiteral("处理中");
    if (status == QLatin1String("review")) return QStringLiteral("待复核");
    if (status == QLatin1String("closed")) return QStringLiteral("已关闭");
    if (status == QLatin1String("reopened")) return QStringLiteral("已重开");
    return status;
}

QString DefectService::priorityText(const QString &priority) {
    if (priority == QLatin1String("high")) return QStringLiteral("高");
    if (priority == QLatin1String("medium")) return QStringLiteral("中");
    return QStringLiteral("低");
}

QVariantMap DefectService::defectToMap(const InspectionDefect &d) const {
    QVariantMap m;
    m.insert(QStringLiteral("id"), d.id);
    m["canEdit"]=AccessPolicy(db_,session_).defectEditable(d.id);
    m["canManage"]=AccessPolicy(db_,session_).permission("assign");
    m.insert(QStringLiteral("code"), d.code);
    m.insert(QStringLiteral("recordId"), d.recordId);
    m.insert(QStringLiteral("assetId"), d.assetId);
    m.insert(QStringLiteral("title"), d.title);
    m.insert(QStringLiteral("description"), d.description);
    m.insert(QStringLiteral("status"), d.status);
    m.insert(QStringLiteral("statusText"), statusText(d.status));
    m.insert(QStringLiteral("priority"), d.priority);
    m.insert(QStringLiteral("priorityText"), priorityText(d.priority));
    m.insert(QStringLiteral("assigneeId"), d.assigneeId);
    m.insert(QStringLiteral("assigneeName"), d.assigneeName);
    m.insert(QStringLiteral("dueAt"), d.dueAt);
    m.insert(QStringLiteral("createdAt"), d.createdAt);
    m.insert(QStringLiteral("closedAt"), d.closedAt);
    const InspectionAsset a = db_->findAssetById(d.assetId);
    m.insert(QStringLiteral("assetCode"), a.code);
    m.insert(QStringLiteral("assetName"), a.name);
    return m;
}

QVariantList DefectService::defects(const QString &statusFilter) const {
    QVariantList out;
    const auto all = db_->loadDefects(statusFilter);
    for (const InspectionDefect &d : all) if(AccessPolicy(db_,session_).defect(d.id))out.push_back(defectToMap(d));
    return out;
}

QVariantMap DefectService::defect(const QString &id) const {
    QVariantMap out;
    if(!AccessPolicy(db_,session_).defect(id))return {};
    const InspectionDefect d = db_->findDefect(id);
    if (!d.id.isEmpty()) out = defectToMap(d);
    return out;
}

QVariantList DefectService::events(const QString &defectId) const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).defect(defectId))return out;
    const auto evs = db_->loadDefectEvents(defectId);
    for (const DefectEvent &e : evs) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("eventType"), e.eventType);
        m.insert(QStringLiteral("status"), e.status);
        m.insert(QStringLiteral("statusText"), statusText(e.status));
        m.insert(QStringLiteral("note"), e.note);
        m.insert(QStringLiteral("actorName"), e.actorName);
        m.insert(QStringLiteral("createdAt"), e.createdAt);
        out.push_back(m);
    }
    return out;
}

QVariantList DefectService::users() const {
    QVariantList out;
    if(!AccessPolicy(db_,session_).permission("assign"))return out;
    const auto users = db_->loadUsers();
    for (const UserInfo &u : users) {
        if (!u.active) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), u.id);
        m.insert(QStringLiteral("name"), u.name);
        m.insert(QStringLiteral("role"), u.role);
        out.push_back(m);
    }
    return out;
}

QVariantMap DefectService::createDefect(const QString &recordId, const QString &assetId,
                                        const QString &title, const QString &description,
                                        const QString &priority,const QString &checkItemKey) {
    if(!AccessPolicy(db_,session_).permission("register_defect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    if(db_->findAssetById(assetId).id.isEmpty())return {{"ok",false},{"error",QStringLiteral("请选择有效设备")}};
    if(!QStringList{"low","medium","high"}.contains(priority))return {{"ok",false},{"error",QStringLiteral("优先级无效")}};
    if(recordId.isEmpty()&&!AccessPolicy(db_,session_).assetWork(assetId))return {{"ok",false},{"error",QStringLiteral("只能为本人负责的设备登记缺陷")}};
    if(!recordId.isEmpty()) {
        if(!AccessPolicy(db_,session_).recordEditable(recordId))return {{"ok",false},{"error",QStringLiteral("不能为他人记录或已提交记录追加缺陷")}};
        bool found=false;for(const auto &r:db_->loadInspectionRecords())if(r.id==recordId&&r.assetId==assetId){found=true;break;}
        if(!found)return {{"ok",false},{"error",QStringLiteral("关联巡检记录无效")}};
    }
    InspectionDefect d;
    d.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d.recordId = recordId;d.checkItemKey=checkItemKey;
    if(!checkItemKey.isEmpty()){QSqlQuery q(db_->connection());q.prepare("SELECT id FROM inspection_check_items WHERE record_id=? AND item_key=? AND is_abnormal=1");q.addBindValue(recordId);q.addBindValue(checkItemKey);if(!q.exec()||!q.next())return {{"ok",false},{"error",QStringLiteral("关联异常检查项无效")}};}
    d.assetId = assetId;
    d.title = title.trimmed().isEmpty() ? QStringLiteral("巡检异常待处理") : title.trimmed();
    d.description = description.trimmed();
    d.status = QStringLiteral("unassigned");
    d.priority = priority.isEmpty() ? QStringLiteral("medium") : priority;
    d.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString err;
    // 编号：DEF-YYYYMMDD-NNN
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
    d.code = db_->nextCode(QStringLiteral("DEF"), date);
    if (!db_->createDefect(d, &err)) { out.insert(QStringLiteral("error"), err); return out; }
    db_->appendAudit(QStringLiteral("create"), QStringLiteral("defect"), d.id, d.code, &err);
    out[QStringLiteral("ok")] = true;
    out.insert(QStringLiteral("id"), d.id);
    out.insert(QStringLiteral("code"), d.code);
    emit defectsChanged();
    return out;
}

QVariantMap DefectService::transition(const QString &defectId, const QString &newStatus,
                                      const QString &eventType, const QString &note,const QVariantMap &fields) {
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionDefect d = db_->findDefect(defectId);
    if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
    // 已关闭缺陷不可再流转（复核失败→重新打开在 review 内处理）
    if (d.status == QLatin1String("closed")) {
        out.insert(QStringLiteral("error"), QStringLiteral("缺陷已关闭，不能再修改")); return out;
    }
    QString err;
    const QString actor = currentActor_.isEmpty() ? d.assigneeName : currentActor_;
    if (!db_->transitionDefect(defectId, newStatus, eventType, note, actor, &err,fields)) {
        out.insert(QStringLiteral("error"), err); return out;
    }
    out[QStringLiteral("ok")] = true;
    emit defectsChanged();
    emit defectUpdated(defectId);
    return out;
}

QVariantMap DefectService::assign(const QString &defectId, const QString &assigneeId,
                                  const QString &dueAt, const QString &note) {
    if(!AccessPolicy(db_,session_).permission("assign"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionDefect d = db_->findDefect(defectId);
    if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
    if (d.status != QLatin1String("unassigned") && d.status != QLatin1String("pending")) {
        out.insert(QStringLiteral("error"), QStringLiteral("当前状态不能分派（%1）").arg(statusText(d.status)));
        return out;
    }
    if (assigneeId.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("请选择处理人")); return out; }
    if (!dueAt.isEmpty() && !QDate::fromString(dueAt,Qt::ISODate).isValid()) {
        out.insert(QStringLiteral("error"),QStringLiteral("处理期限请填写有效日期，如 2026-10-30")); return out;
    }
    QString err;
    // 更新处理人与截止日期
    QString prio = d.priority;
    QSqlQuery valid(db_->connection());valid.prepare("SELECT id FROM users WHERE id=? AND active=1 AND password_hash<>''");valid.addBindValue(assigneeId);if(!valid.exec()||!valid.next())return {{"ok",false},{"error",QStringLiteral("接收人未启用")}};
    return transition(defectId, QStringLiteral("pending"), QStringLiteral("assign"),
                      note.isEmpty() ? QStringLiteral("分派处理人") : note,{{"assignee_id",assigneeId},{"due_at",dueAt},{"description",d.description}});
}

QVariantMap DefectService::start(const QString &defectId, const QString &note) {
    if(!AccessPolicy(db_,session_).permission("start_defect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionDefect d = db_->findDefect(defectId);
    if(!session_->can("assign")&&d.assigneeId!=session_->userId())return {{"ok",false},{"error",QStringLiteral("只能处理分派给本人的缺陷")}};
    if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
    if (d.status != QLatin1String("pending")) {
        out.insert(QStringLiteral("error"), QStringLiteral("仅待处理缺陷可开始处理")); return out;
    }
    return transition(defectId, QStringLiteral("in_progress"), QStringLiteral("start"),
                      note.isEmpty() ? QStringLiteral("开始处理") : note);
}

QVariantMap DefectService::submitFeedback(const QString &defectId, const QString &feedback) {
    if(!AccessPolicy(db_,session_).permission("start_defect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionDefect d = db_->findDefect(defectId);
    if(!session_->can("assign")&&d.assigneeId!=session_->userId())return {{"ok",false},{"error",QStringLiteral("只能处理分派给本人的缺陷")}};
    if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
    if (feedback.trimmed().isEmpty()) {
        out.insert(QStringLiteral("error"), QStringLiteral("未填写整改结果，不能申请复核"));
        return out;
    }
    if (d.status != QLatin1String("in_progress")) {
        out.insert(QStringLiteral("error"), QStringLiteral("仅处理中的缺陷可提交整改结果"));
        return out;
    }
    // 将整改结果写入 description 尾部（保留原始描述）
    QString desc = d.description;
    if (!desc.isEmpty()) desc += QStringLiteral("\n");
    desc += QStringLiteral("【整改结果 %1】%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), feedback.trimmed());
    QString err;
    return transition(defectId, QStringLiteral("review"), QStringLiteral("feedback"),
                      QStringLiteral("整改结果：%1").arg(feedback.trimmed()),{{"description",desc}});
}

QVariantMap DefectService::review(const QString &defectId, bool passed, const QString &comment) {
    if(!AccessPolicy(db_,session_).permission("review"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionDefect d = db_->findDefect(defectId);
    if (d.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("缺陷单不存在")); return out; }
    if (d.status != QLatin1String("review")) {
        out.insert(QStringLiteral("error"), QStringLiteral("仅待复核缺陷可执行复核"));
        return out;
    }
    if (!passed && comment.trimmed().isEmpty()) {
        out.insert(QStringLiteral("error"), QStringLiteral("复核未通过必须填写原因"));
        return out;
    }
    QString desc = d.description;
    desc += QStringLiteral("\n【复核 %1】%2").arg(passed ? QStringLiteral("通过") : QStringLiteral("未通过"),
                                                  comment.trimmed().isEmpty() ? QStringLiteral("复核通过") : comment.trimmed());
    QString err;
    if (passed) {
        return transition(defectId, QStringLiteral("closed"), QStringLiteral("close"),
                          QStringLiteral("复核通过并关闭"),{{"description",desc}});
    }
    return transition(defectId, QStringLiteral("in_progress"), QStringLiteral("reopen"),
                      QStringLiteral("复核未通过，重新打开：%1").arg(comment.trimmed()),{{"description",desc}});
}

bool DefectService::canAddEvidence(const QString &defectId) const {
    const InspectionDefect d = db_->findDefect(defectId);
    return AccessPolicy(db_,session_).permission("inspect")&&AccessPolicy(db_,session_).defect(defectId)&&d.status!="closed"&&(AccessPolicy(db_,session_).manages()||d.assigneeId==session_->userId()||AccessPolicy(db_,session_).defectEditable(defectId));
}


QVariantMap DefectService::saveDefect(const QString &id,const QString &assetId,const QString &title,const QString &description,const QString &priority) {
    if(!AccessPolicy(db_,session_).permission("register_defect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    if(title.trimmed().isEmpty())return {{"ok",false},{"error",QStringLiteral("请填写缺陷标题")}};
    if(id.isEmpty())return createDefect("",assetId,title,description,priority);
    auto d=db_->findDefect(id);if(d.id.isEmpty())return {{"ok",false},{"error",QStringLiteral("缺陷不存在")}};
    if(!assetId.isEmpty()&&assetId!=d.assetId)return {{"ok",false},{"error",QStringLiteral("不能更换缺陷的来源设备")}};
    if(!AccessPolicy(db_,session_).defectEditable(id))return {{"ok",false},{"error",QStringLiteral("仅管理人员或未分派缺陷的登记人可编辑原始内容；整改请提交反馈")}};
    if(!QStringList{"low","medium","high"}.contains(priority))return {{"ok",false},{"error",QStringLiteral("优先级无效")}};
    QString error;if(!db_->saveDefect(id,title.trimmed(),description.trimmed(),priority,&error))return {{"ok",false},{"error",error}};
    emit defectsChanged();emit defectUpdated(id);return {{"ok",true},{"id",id}};
}
QVariantMap DefectService::deleteDefect(const QString &id) {
    if(!AccessPolicy(db_,session_).permission("assign"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};QString e;if(!db_->removeEntity("defect",id,&e))return {{"ok",false},{"error",e}};emit defectsChanged();return {{"ok",true}};}
