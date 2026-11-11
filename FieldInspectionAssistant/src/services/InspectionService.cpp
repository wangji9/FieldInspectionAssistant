#include "InspectionService.h"
#include "AccessPolicy.h"
#include "../app/SessionManager.h"
#include <cmath>
#include "../database/DatabaseManager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

InspectionService::InspectionService(DatabaseManager *db, QObject *parent)
    : QObject(parent), db_(db) {}

QString InspectionService::statusText(const QString &status) {
    if (status == QLatin1String("pending")) return QStringLiteral("待开始");
    if (status == QLatin1String("in_progress")) return QStringLiteral("进行中");
    if (status == QLatin1String("completed")) return QStringLiteral("已完成");
    if (status == QLatin1String("overdue")) return QStringLiteral("已逾期");
    return status;
}

QVariantMap InspectionService::taskToMap(const InspectionTask &t) {
    QVariantMap m;
    m.insert(QStringLiteral("id"), t.id);
    m.insert(QStringLiteral("code"), t.code);
    m.insert(QStringLiteral("name"), t.name);
    m.insert(QStringLiteral("status"), t.status);
    m.insert(QStringLiteral("statusText"), statusText(t.status));
    m.insert(QStringLiteral("progressText"), t.progressText);
    m.insert(QStringLiteral("plannedAt"), t.plannedAt);
    m.insert(QStringLiteral("assetName"), t.assetName);
    m.insert(QStringLiteral("assetId"), t.assetId);
    return m;
}

QVariantMap InspectionService::itemToMap(const CheckItemResult &it) {
    QVariantMap m;
    m.insert(QStringLiteral("key"), it.key);
    m.insert(QStringLiteral("name"), it.name);
    m.insert(QStringLiteral("type"), it.type);
    m.insert(QStringLiteral("unit"), it.unit);
    m.insert(QStringLiteral("result"), it.result);
    m.insert(QStringLiteral("value"), it.value);
    m.insert(QStringLiteral("thresholdText"), it.thresholdText);
    m.insert(QStringLiteral("abnormal"), it.abnormal);
    m.insert(QStringLiteral("note"), it.note);
    m.insert(QStringLiteral("imagePath"), it.imagePath);
    m.insert(QStringLiteral("resultText"), it.abnormal ? QStringLiteral("异常") : it.result == QLatin1String("normal") ? QStringLiteral("正常")
                                           : it.result == QLatin1String("abnormal") ? QStringLiteral("异常")
                                           : it.result == QLatin1String("skip") ? QStringLiteral("跳过")
                                           : QStringLiteral("未填写"));
    return m;
}

bool InspectionService::ownsTask(const QString&id)const{return AccessPolicy(db_,session_).task(id);}
QString InspectionService::recordTask(const QString&id)const{QSqlQuery q(db_->connection());q.prepare("SELECT task_id FROM inspection_records WHERE id=?");q.addBindValue(id);return q.exec()&&q.next()?q.value(0).toString():QString();}
bool InspectionService::freezeTemplate(const QString&id){
    QSqlQuery q(db_->connection());q.prepare("SELECT task_id FROM task_settings WHERE task_id=?");q.addBindValue(id);if(q.exec()&&q.next())return true;
    auto t=db_->defaultTemplate();if(t.id.isEmpty())return false;
    auto snapshot=QJsonDocument(QJsonObject{{"id",t.id},{"name",t.name},{"version",t.version},{"definition",QJsonDocument::fromJson(t.definitionJson.toUtf8()).object()}}).toJson(QJsonDocument::Compact);
    q.prepare("INSERT INTO task_settings(task_id,template_snapshot,kind) VALUES(?,?,'temporary')");q.addBindValue(id);q.addBindValue(QString::fromUtf8(snapshot));return q.exec();
}
QVariantList InspectionService::parseTemplateItems(const QString &taskId) const {
    QVariantList out;
    const TemplateInfo tmpl = db_->defaultTemplate();
    if (tmpl.id.isEmpty()) return out;
    QJsonParseError err;
    QByteArray definition=tmpl.definitionJson.toUtf8();
    if(!taskId.isEmpty()){QSqlQuery q(db_->connection());q.prepare("SELECT template_snapshot FROM task_settings WHERE task_id=?");q.addBindValue(taskId);if(q.exec()&&q.next())definition=QJsonDocument(QJsonDocument::fromJson(q.value(0).toByteArray()).object()["definition"].toObject()).toJson();}
    const QJsonDocument doc = QJsonDocument::fromJson(definition, &err);
    if (err.error != QJsonParseError::NoError) return out;
    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("key"), o.value(QStringLiteral("key")).toString());
        m.insert(QStringLiteral("name"), o.value(QStringLiteral("name")).toString());
        m.insert(QStringLiteral("type"), o.value(QStringLiteral("type")).toString());
        m.insert(QStringLiteral("unit"), o.value(QStringLiteral("unit")).toString());
        m.insert(QStringLiteral("options"), o.value(QStringLiteral("options")).toString());
        m.insert(QStringLiteral("min"), o.value(QStringLiteral("min")).toVariant().toString());
        m.insert(QStringLiteral("max"), o.value(QStringLiteral("max")).toVariant().toString());
        m.insert(QStringLiteral("thresholdText"), o.value(QStringLiteral("thresholdText")).toString());
        out.push_back(m);
    }
    return out;
}

QVariantList InspectionService::templateItems(const QString &taskId) const {if(!AccessPolicy(db_,session_).authenticated()||(!taskId.isEmpty()&&!ownsTask(taskId)))return {}; return parseTemplateItems(taskId); }

// 任务资产：任务指定设备时返回该设备，否则返回全部在用设备
QVariantList InspectionService::taskAssets(const QString &taskId) {
    QVariantList out;
    if(!AccessPolicy(db_,session_).task(taskId))return {};
    const InspectionTask task = db_->findTask(taskId);
    if(task.id.isEmpty())return out;
    QVector<InspectionAsset> assets;
    if (!task.assetId.isEmpty()) {
        const InspectionAsset a = db_->findAssetById(task.assetId);
        if (!a.id.isEmpty()) assets.push_back(a);
    } else {
        assets = db_->loadAssets();
    }
    for (const InspectionAsset &a : assets) {
        const QVariantMap rec = recordFor(taskId, a.id);
        QVariantMap m;
        m.insert(QStringLiteral("id"), a.id);
        m.insert(QStringLiteral("code"), a.code);
        m.insert(QStringLiteral("name"), a.name);
        m.insert(QStringLiteral("location"), a.location);
        m.insert(QStringLiteral("recordId"), rec.value(QStringLiteral("recordId")));
        out.push_back(m);
    }
    return out;
}

QVariantMap InspectionService::recordFor(const QString &taskId, const QString &assetId) {
    if(!ownsTask(taskId))return {{"ok",false},{"error",QStringLiteral("只能处理本人负责的任务")}};
    if(!AccessPolicy(db_,session_).permission("inspect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out;
    const auto task=db_->findTask(taskId);
    if(task.id.isEmpty()||db_->findAssetById(assetId).id.isEmpty())return {{"error",QStringLiteral("任务或设备不存在")}};
    const QVector<InspectionRecord> recs = db_->loadInspectionRecords();
    for (const InspectionRecord &r : recs) {
        if (r.taskId == taskId && r.assetId == assetId) {
            out.insert(QStringLiteral("recordId"), r.id);
            out.insert(QStringLiteral("answered"), progress(r.id).size());
            out.insert(QStringLiteral("total"), parseTemplateItems(taskId).size());
            return out;
        }
    }
    QSqlQuery recycled(db_->connection());recycled.prepare("SELECT id FROM inspection_records WHERE task_id=? AND asset_id=?");recycled.addBindValue(taskId);recycled.addBindValue(assetId);if(recycled.exec()&&recycled.next())return {{"error",QStringLiteral("该任务的记录已移入回收站，请恢复原记录")}};
    if(task.status!="in_progress")return {{"error",QStringLiteral("请先开始巡检；已完成的任务不能创建草稿")}};
    if(!task.assetId.isEmpty()&&task.assetId!=assetId)return {{"error",QStringLiteral("设备不属于该任务")}};
    InspectionRecord rec;
    rec.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rec.taskId = taskId;
    rec.assetId = assetId;
    rec.templateName = db_->defaultTemplate().name;
    rec.result = QStringLiteral("草稿");
    rec.status = QStringLiteral("draft");
    rec.time = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString err;
    if (!db_->insertInspection(rec, &err)) {
        out.insert(QStringLiteral("error"), err);
        return out;
    }
    out.insert(QStringLiteral("recordId"), rec.id);
    out.insert(QStringLiteral("answered"), 0);
    out.insert(QStringLiteral("total"), parseTemplateItems(taskId).size());
    emit recordChanged(rec.id);
    return out;
}

QVariantList InspectionService::progress(const QString &recordId) const {
    if(!AccessPolicy(db_,session_).record(recordId))return {};
    QVariantList out;
    const auto items = db_->loadCheckItems(recordId);
    for (const CheckItemResult &it : items) out.push_back(itemToMap(it));
    return out;
}

QVariantList InspectionService::tasks(const QString &statusFilter) const {
    QVariantList out;
    const auto tasks = db_->loadTasks(statusFilter);
    const QVector<InspectionRecord> recs = db_->loadInspectionRecords();
    const int itemCount = parseTemplateItems().size();
    const int assetCount = db_->loadAssets().size();
    for (const InspectionTask &t : tasks) {
        if(!ownsTask(t.id))continue;
        InspectionTask copy = t;
        int answered = 0;
        int taskAssets = t.assetId.isEmpty() ? assetCount : 1;
        int total = taskAssets * itemCount;
        for (const InspectionRecord &r : recs) {
            if (r.taskId != t.id) continue;
            const QVector<CheckItemResult> results = db_->loadCheckItems(r.id);
            for (const CheckItemResult &ci : results) if (!ci.result.isEmpty()) ++answered;
        }
        copy.progressText = QStringLiteral("%1/%2").arg(answered).arg(total);
        out.push_back(taskToMap(copy));
    }
    return out;
}

QVariantList InspectionService::inspectionCandidates(const QString &assetId)const{
    QVariantList rows;auto access=AccessPolicy(db_,session_);if(!access.permission("inspect"))return rows;
    QSqlQuery q(db_->connection());q.prepare("SELECT t.id,t.task_code,t.name,t.status,t.planned_at,u.name,r.id,r.status,EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='record' AND z.entity_id=r.id AND z.restored_at='') FROM inspection_tasks t JOIN assets a ON a.id=t.asset_id AND a.active=1 LEFT JOIN users u ON u.id=t.assignee_id LEFT JOIN inspection_records r ON r.task_id=t.id AND r.asset_id=t.asset_id WHERE t.asset_id=? AND t.status IN ('pending','overdue','in_progress') AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='task' AND z.entity_id=t.id AND z.restored_at='') ORDER BY CASE t.status WHEN 'in_progress' THEN 0 ELSE 1 END,t.planned_at,t.id");q.addBindValue(assetId);if(!q.exec())return rows;
    while(q.next()){
        if(!access.task(q.value(0).toString()))continue;
        bool recycled=q.value(8).toBool(),submitted=q.value(7).toString()=="submitted";
        rows<<QVariantMap{{"id",q.value(0)},{"code",q.value(1)},{"name",q.value(2)},{"display",q.value(1).toString()+" · "+q.value(2).toString()+" · "+statusText(q.value(3).toString())},{"status",q.value(3)},{"planned_at",q.value(4)},{"assignee",q.value(5)},{"record_id",q.value(6)},{"can_start",!recycled&&!submitted},{"reason",recycled?"原巡检记录已在回收站，请先恢复原记录":submitted?"原巡检记录已提交，请查看原记录":""}};
    }return rows;
}
QVariantMap InspectionService::beginInspection(const QString &assetId,const QString &taskId){
    auto fail=[](const QString&e){return QVariantMap{{"ok",false},{"error",e}};};
    auto access=AccessPolicy(db_,session_);if(!access.permission("inspect")||!access.task(taskId))return fail("无权开始此任务，请选择分派给自己的任务");
    auto db=db_->connection();if(!db.transaction())return fail("数据库暂时不可写，请重试");
    auto reject=[&](const QString&e){db.rollback();return fail(e);};
    auto task=db_->findTask(taskId);QSqlQuery q(db);q.prepare("SELECT id FROM assets WHERE id=? AND active=1");q.addBindValue(assetId);
    if(!q.exec()||!q.next()||task.assetId!=assetId)return reject("设备与任务不匹配，请重新选择");
    if(task.status!="pending"&&task.status!="overdue"&&task.status!="in_progress")return reject("此任务已结束，不能再新增巡检");
    q.prepare("SELECT r.id,r.status,EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='record' AND z.entity_id=r.id AND z.restored_at='') FROM inspection_records r WHERE r.task_id=? AND r.asset_id=?");q.addBindValue(taskId);q.addBindValue(assetId);if(!q.exec())return reject(q.lastError().text());
    QString recordId;bool resumed=q.next();if(resumed){recordId=q.value(0).toString();if(q.value(2).toBool())return reject("原巡检记录已在回收站，请先恢复原记录");if(q.value(1).toString()!="draft")return reject("原巡检记录已提交，请查看档案");}
    if(!freezeTemplate(taskId)||parseTemplateItems(taskId).isEmpty())return reject("任务缺少有效检查项目，请先完善巡检模板");
    QString error;
    if(task.status!="in_progress"&&!db_->updateTaskStatus(taskId,"in_progress",&error))return reject(error);
    if(!resumed){
        recordId=QUuid::createUuid().toString(QUuid::WithoutBraces);q.prepare("SELECT template_snapshot FROM task_settings WHERE task_id=?");q.addBindValue(taskId);q.exec();q.next();auto snapshot=QJsonDocument::fromJson(q.value(0).toByteArray()).object();
        const QString stamp=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        q.prepare("INSERT INTO inspection_records(id,task_id,asset_id,template_name,result,answer,image_path,status,created_at) VALUES(?,?,?,?,'草稿','','','draft',?)");q.addBindValue(recordId);q.addBindValue(taskId);q.addBindValue(assetId);q.addBindValue(snapshot.value("name").toString(db_->defaultTemplate().name));q.addBindValue(stamp);
        if(!q.exec())return reject(q.lastError().text());
        if(!db_->appendAudit("create","inspection_record",recordId,QString::fromUtf8(QJsonDocument(QJsonObject{{"task_id",taskId},{"asset_id",assetId}}).toJson(QJsonDocument::Compact)),&error)||!db_->enqueueSync("inspection_record",recordId,"create",recordId,&error))return reject(error);
    }
    if(!db.commit())return reject(db.lastError().text());
    emit tasksChanged();emit recordChanged(recordId);
    return {{"ok",true},{"record_id",recordId},{"task_id",taskId},{"task_name",task.name},{"task_code",task.code},{"resumed",resumed}};
}

QVariantMap InspectionService::startTask(const QString &taskId) {
    if(!ownsTask(taskId))return {{"ok",false},{"error",QStringLiteral("只能处理本人负责的任务")}};
    if(!AccessPolicy(db_,session_).permission("inspect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionTask t = db_->findTask(taskId);
    if (t.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("任务不存在")); return out; }
    if(t.status=="completed")return {{"ok",false},{"error",QStringLiteral("任务已完成，请查看档案")}};
    if(!freezeTemplate(taskId))return {{"ok",false},{"error",QStringLiteral("无法保存标准版本")}};
    if(db_->loadAssets().isEmpty()||parseTemplateItems(taskId).isEmpty())return {{"ok",false},{"error",QStringLiteral("请先添加设备和巡检项目")}};
    if (t.status == QLatin1String("pending")||t.status=="overdue") {
        QString err;
        if (!db_->updateTaskStatus(taskId, QStringLiteral("in_progress"), &err)) {
            out.insert(QStringLiteral("error"), err); return out;
        }
    }
    out[QStringLiteral("ok")] = true;
    emit tasksChanged();
    return out;
}

QVariantMap InspectionService::saveItem(const QString &recordId, const QString &itemKey,
                                        const QString &result, const QString &value, const QString &note) {
    if(!AccessPolicy(db_,session_).permission("inspect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    if(!AccessPolicy(db_,session_).recordEditable(recordId))return {{"ok",false},{"error",QStringLiteral("无权修改该记录")}};
    bool editable=false;
    for(const auto &record:db_->loadInspectionRecords())if(record.id==recordId)editable=record.status=="draft"&&db_->findTask(record.taskId).status=="in_progress";
    if(!editable)return {{"ok",false},{"error",QStringLiteral("当前记录不能修改")}};
    if(!QStringList{"normal","abnormal","skip"}.contains(result))return {{"ok",false},{"error",QStringLiteral("检查结果无效")}};
    if (result == QLatin1String("skip") && note.trimmed().isEmpty()) {
        out.insert(QStringLiteral("error"), QStringLiteral("跳过必须填写原因"));
        return out;
    }
    const QVariantList items = parseTemplateItems(recordTask(recordId));
    CheckItemResult item;
    bool found = false;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("key")).toString() == itemKey) {
            item.key = itemKey;
            item.name = m.value(QStringLiteral("name")).toString();
            item.type = m.value(QStringLiteral("type")).toString();
            item.unit = m.value(QStringLiteral("unit")).toString();
            item.thresholdText = m.value(QStringLiteral("thresholdText")).toString();
            found = true;
            break;
        }
    }
    if (!found) { out.insert(QStringLiteral("error"), QStringLiteral("检查项不存在")); return out; }

    if(item.type=="measure"&&result!="skip"){bool ok=false;double x=value.toDouble(&ok);if(!ok||!std::isfinite(x))return {{"ok",false},{"error",QStringLiteral("请输入有限的测量数值")}};}
    bool abnormal = (result == QLatin1String("abnormal"));
    if (item.type == QLatin1String("measure") && result == QLatin1String("normal")) {
        bool okVal = false;
        const double v = value.trimmed().toDouble(&okVal);
        if (!okVal) { out.insert(QStringLiteral("error"), QStringLiteral("请填写有效测量值")); return out; }
        QString minText, maxText;
        for (const QVariant &v2 : items) {
            const QVariantMap m = v2.toMap();
            if (m.value(QStringLiteral("key")).toString() == itemKey) { minText = m.value(QStringLiteral("min")).toString(); maxText = m.value(QStringLiteral("max")).toString(); break; }
        }
        abnormal = (!minText.isEmpty() && v < minText.toDouble()) || (!maxText.isEmpty() && v > maxText.toDouble());
        item.value = value.trimmed();
    } else if (item.type == QLatin1String("measure")) {
        item.value = value.trimmed();
    }
    item.result = abnormal ? QStringLiteral("abnormal") : result;
    item.abnormal = abnormal;
    item.note = note.trimmed();

    QString err;
    if(!db_->transaction())return {{"ok",false},{"error",QStringLiteral("无法保存检查项事务")}};
    if (!db_->upsertCheckItem(item, recordId, &err)) {
        db_->rollback();out.insert(QStringLiteral("error"), err); return out;
    }
    QString aerr;
    const auto detail=QString::fromUtf8(QJsonDocument(QJsonObject{{"item_key",item.key},{"item_name",item.name},{"result",result},{"value",item.value},{"unit",item.unit},{"threshold",item.thresholdText},{"note",item.note}}).toJson(QJsonDocument::Compact));
    if(!db_->appendAudit(QStringLiteral("save_item"),QStringLiteral("record"),recordId,detail,&aerr)||!db_->commit()){db_->rollback();return {{"ok",false},{"error",aerr.isEmpty()?QStringLiteral("检查项事务提交失败"):aerr}};}
    out[QStringLiteral("ok")] = true;
    out.insert(QStringLiteral("abnormal"), abnormal);
    out.insert(QStringLiteral("thresholdText"), item.thresholdText);
    emit recordChanged(recordId);
    return out;
}

QVariantMap InspectionService::completeTask(const QString &taskId) {
    if(!ownsTask(taskId))return {{"ok",false},{"error",QStringLiteral("只能处理本人负责的任务")}};
    if(!AccessPolicy(db_,session_).permission("inspect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    QVariantMap out{{QStringLiteral("ok"), false}};
    const InspectionTask t = db_->findTask(taskId);
    if (t.id.isEmpty()) { out.insert(QStringLiteral("error"), QStringLiteral("任务不存在")); return out; }

    if(t.status!="in_progress")return {{"ok",false},{"error",QStringLiteral("请先开始巡检")}};
    const QVector<InspectionRecord> recs = db_->loadInspectionRecords();
    const int itemCount = parseTemplateItems(taskId).size();
    const int assetCount = t.assetId.isEmpty() ? db_->loadAssets().size() : 1;
    const int total = assetCount * itemCount;
    int answered = 0;
    int abnormal = 0;
    QString pendingItem;
    for (const InspectionRecord &r : recs) {
        if (r.taskId != taskId) continue;
        const QVector<CheckItemResult> results = db_->loadCheckItems(r.id);
        for (const CheckItemResult &ci : results) {
            if (!ci.result.isEmpty()) ++answered;
            if (ci.abnormal) {
                ++abnormal;
                if (pendingItem.isEmpty()) pendingItem = ci.name;
            }
        }
    }
    if(total==0)return {{"ok",false},{"error",QStringLiteral("任务没有设备或检查项，不能提交")}};
    if (answered < total) {
        out.insert(QStringLiteral("error"), QStringLiteral("还有 %1 项检查未填写，请先完成全部检查项").arg(total - answered));
        return out;
    }
    for(const auto &r:recs){if(r.taskId!=taskId)continue;for(const auto &ci:db_->loadCheckItems(r.id)){if(!ci.abnormal)continue;QSqlQuery link(db_->connection());link.prepare("SELECT id FROM defects WHERE record_id=? AND check_item_key=?");link.addBindValue(r.id);link.addBindValue(ci.key);if(!link.exec()||!link.next())return {{"ok",false},{"error",QStringLiteral("异常项目 %1 尚未关联对应缺陷单").arg(ci.name)}};}}

    QString err;
    if(!db_->transaction())return {{"ok",false},{"error",QStringLiteral("无法提交事务")}};
    for (const InspectionRecord &r : recs) {
        if (r.taskId != taskId) continue;
        int count=0,skipped=0,totalItems=0;
        for(const auto &ci:db_->loadCheckItems(r.id)){totalItems++;if(ci.abnormal)++count;if(ci.result=="skip")skipped++;}
        QString finalResult=count?(skipped?QStringLiteral("异常（部分未检）"):QStringLiteral("异常")):(skipped==totalItems?QStringLiteral("未检（全部跳过）"):skipped?QStringLiteral("部分检查"):QStringLiteral("正常"));
        if(!db_->updateRecordStatus(r.id,"submitted",finalResult,QStringLiteral("任务完成"),&err)){db_->rollback();return {{"ok",false},{"error",err}};}
    }
    if (!db_->updateTaskStatus(taskId, QStringLiteral("completed"), &err)) {
        db_->rollback();out.insert(QStringLiteral("error"), err); return out;
    }
    db_->appendAudit(QStringLiteral("complete_task"), QStringLiteral("task"), taskId, t.code, &err);
    if(!db_->commit())return {{"ok",false},{"error",QStringLiteral("保存提交结果失败")}};
    out[QStringLiteral("ok")] = true;
    emit tasksChanged();
    emit taskCompleted(t.code);
    return out;
}

QVariantMap InspectionService::saveTask(const QString &id,const QString &name,const QString &assetId,const QString &plannedAt) {
    if(!AccessPolicy(db_,session_).permission("assign"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    if(name.trimmed().isEmpty())return {{"ok",false},{"error",QStringLiteral("请填写任务名称")}};
    if(db_->findAssetById(assetId).id.isEmpty())return {{"ok",false},{"error",QStringLiteral("请选择设备，请先在设备中心新增设备")}};
    auto date=QDateTime::fromString(plannedAt,Qt::ISODate);if(!date.isValid())date=QDateTime::fromString(plannedAt,"yyyy-MM-dd HH:mm");
    if(!date.isValid())return {{"ok",false},{"error",QStringLiteral("计划时间格式：yyyy-MM-dd HH:mm")}};
    auto task=db_->findTask(id);QString error,taskId=id;
    if(!id.isEmpty()) {
        if(task.id.isEmpty())return {{"ok",false},{"error",QStringLiteral("任务不存在")}};
        if(task.status=="completed")return {{"ok",false},{"error",QStringLiteral("已完成任务请查阅档案")}};
        if(task.status!="pending"&&task.assetId!=assetId)return {{"ok",false},{"error",QStringLiteral("开始巡检后不能更换设备")}};
        if(!db_->saveTask(id,name.trimmed(),assetId,date.toString(Qt::ISODate),&error))return {{"ok",false},{"error",error}};
    } else {
        taskId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        InspectionTask t;t.id=taskId;t.code=db_->nextCode("INSP",QDate::currentDate().toString("yyyyMMdd"));t.name=name.trimmed();t.assetId=assetId;t.plannedAt=date.toString(Qt::ISODate);t.status="pending";t.createdAt=QDateTime::currentDateTime().toString(Qt::ISODate);
        if(!db_->createTask(t,&error))return {{"ok",false},{"error",error}};
    }
    QSqlQuery owner(db_->connection());owner.prepare("UPDATE inspection_tasks SET assignee_id=? WHERE id=? AND (assignee_id IS NULL OR assignee_id='')");owner.addBindValue(session_->userId());owner.addBindValue(taskId);if(!owner.exec()||!freezeTemplate(taskId))return {{"ok",false},{"error",QStringLiteral("任务负责人或标准版本保存失败")}};
    emit tasksChanged();return {{"ok",true},{"id",taskId}};
}
QVariantMap InspectionService::deleteTask(const QString &id) {
    if(!AccessPolicy(db_,session_).permission("assign"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};QString e;if(!db_->removeEntity("task",id,&e))return {{"ok",false},{"error",e}};emit tasksChanged();return {{"ok",true}};}
QVariantMap InspectionService::saveArchive(const QString &id,const QString &assetId,const QString &result,const QString &answer) {
    if(!AccessPolicy(db_,session_).permission("inspect"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};
    if(result.trimmed().isEmpty())return {{"ok",false},{"error",QStringLiteral("请填写巡检结果")}};
    QString error,recordId=id;
    if(id.isEmpty()) {
        return {{"ok",false},{"error",QStringLiteral("请先派发并开始任务，在对应任务下生成巡检记录")}};
    } else {
        if(!AccessPolicy(db_,session_).recordEditable(id))return {{"ok",false},{"error",QStringLiteral("记录不属于当前用户或已锁定")}};
        bool found=false;for(const auto &r:db_->loadInspectionRecords())if(r.id==id){found=true;if(r.status=="submitted")return {{"ok",false},{"error",QStringLiteral("已提交档案请通过正式报告更正，不能覆盖")}};if(!db_->updateRecordStatus(id,r.status,result.trimmed(),answer,&error))return {{"ok",false},{"error",error}};break;}
        if(!found)return {{"ok",false},{"error",QStringLiteral("档案不存在")}};
    }
    emit tasksChanged();emit recordChanged(recordId);return {{"ok",true},{"id",recordId}};
}
QVariantMap InspectionService::deleteArchive(const QString &id) {
    if(!AccessPolicy(db_,session_).permission("assign"))return {{"ok",false},{"error",QStringLiteral("没有操作权限")}};QString e;if(!db_->removeEntity("record",id,&e))return {{"ok",false},{"error",e}};emit tasksChanged();emit recordChanged(id);return {{"ok",true}};}
