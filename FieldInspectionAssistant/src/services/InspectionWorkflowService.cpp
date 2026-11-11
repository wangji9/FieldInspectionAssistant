#include "InspectionWorkflowService.h"
#include "WorkspaceService.h"
#include "AccessPolicy.h"
#include "../database/DatabaseManager.h"
#include "../app/SessionManager.h"
#include <QImageReader>
#include <QSaveFile>
namespace {
QString now(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
QString json(const QVariant &value){return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));}
QString text(const QVariantMap &v,const char *key){auto value=v.value(key).toString();return value.isNull()?QStringLiteral(""):value.trimmed();}
QVariantMap fail(const QString &s){return {{"ok",false},{"error",s}};}
QVariantMap row(QSqlQuery &q){QVariantMap r;auto record=q.record();for(int i=0;i<record.count();++i)r[record.fieldName(i)]=q.value(i);return r;}
QVariantMap one(QSqlDatabase db,const QString &sql,const QVariantList &args){QSqlQuery q(db);q.prepare(sql);for(auto v:args)q.addBindValue(v);return q.exec()&&q.next()?row(q):QVariantMap{};}
}
InspectionWorkflowService::InspectionWorkflowService(DatabaseManager*d,SessionManager*s,WorkspaceService*w,QObject*p):QObject(p),db_(d),session_(s),workspace_(w){}
bool InspectionWorkflowService::canWrite(const QString &id)const{
    auto a=AccessPolicy(db_,session_);if(!a.permission("inspect")||!a.record(id))return false;
    if(a.manages())return true;
    return !one(db_->connection(),"SELECT r.id FROM inspection_records r JOIN inspection_tasks t ON t.id=r.task_id WHERE r.id=? AND t.assignee_id=?",{id,session_->userId()}).isEmpty();
}
QVariantList InspectionWorkflowService::entries(const QString &id,bool deleted)const{
    QVariantList result;if(!AccessPolicy(db_,session_).record(id))return result;QSqlQuery q(db_->connection());q.prepare("SELECT e.*,u.name AS creator FROM product_evidence e LEFT JOIN users u ON u.id=e.created_by WHERE e.record_id=? ORDER BY e.created_at,e.rowid");q.addBindValue(id);q.exec();
    while(q.next()){auto v=row(q);auto m=QJsonDocument::fromJson(v["metadata"].toByteArray()).object().toVariantMap();if(!m.value("workflow_entry").toBool())continue;if(!deleted&&!v["deleted_at"].toString().isEmpty())continue;v["metadata"]=m;v["stage"]=m["stage"];v["state"]=m["state"];v["revision"]=m.value("revision",1);v["editable"]=canWrite(id);result<<v;}return result;
}
QVariantList InspectionWorkflowService::flow(const QVariantMap &task,const QVariantMap &record,const QVariantList &defects,const QVariantList &notes)const{
    int open=0,review=0,pending=0;for(auto v:defects){auto d=v.toMap();if(d["status"]!="closed")++open;if(d["status"]=="review")++review;}for(auto v:notes)if(v.toMap().value("state")!="completed")++pending;
    bool started=task.value("status")=="in_progress"||task.value("status")=="completed"||!record.isEmpty();bool submitted=record.value("status")=="submitted";
    auto stage=[](const QString&id,const QString&name,const QString&state,const QString&detail){return QVariantMap{{"id",id},{"name",name},{"state",state},{"detail",detail}};};
    return {stage("dispatch","派发",task.isEmpty()?"pending":"completed",task.value("assignee_name").toString()),stage("inspect","巡检",submitted?"completed":started?"active":"pending",""),stage("record","记录",submitted?"completed":!record.isEmpty()?"active":"pending",""),stage("remediate","整改",submitted&&open==0&&pending==0?"completed":open>0||pending>0?"active":"pending",QString::number(open)+" 缺陷 · "+QString::number(pending)+" 待办"),stage("review","复核",submitted&&open==0&&pending==0?"completed":review>0?"active":"pending",review?QString::number(review)+" 项待复核":""),stage("complete","完成",submitted&&open==0&&pending==0?"completed":"pending","")};
}
QVariantMap InspectionWorkflowService::detail(const QString &kind,const QString &id)const{
    auto access=AccessPolicy(db_,session_);if(!access.authenticated())return fail("请先登录");auto db=db_->connection();QString recordId=id,taskId;QVariantMap task,record,selectedDefect;
    if(kind=="tasks"){
        if(!access.task(id))return fail("任务不存在或无权查看");taskId=id;record=one(db,"SELECT * FROM inspection_records WHERE task_id=? AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='record' AND z.entity_id=inspection_records.id AND z.restored_at='') ORDER BY created_at DESC LIMIT 1",{id});recordId=record.value("id").toString();
    }else if(kind=="defects"){
        if(!access.defect(id))return fail("缺陷不存在或无权查看");selectedDefect=one(db,"SELECT * FROM defects WHERE id=?",{id});recordId=selectedDefect.value("record_id").toString();
    }else if(kind!="records")return fail("记录类型无效");
    if(!recordId.isEmpty()){if(!access.record(recordId))return fail("巡检记录不存在或无权查看");record=one(db,"SELECT * FROM inspection_records WHERE id=?",{recordId});taskId=record.value("task_id").toString();}
    if(!taskId.isEmpty())task=one(db,"SELECT t.*,u.name AS assignee_name FROM inspection_tasks t LEFT JOIN users u ON u.id=t.assignee_id WHERE t.id=?",{taskId});
    QString assetId=record.value("asset_id",task.value("asset_id",selectedDefect.value("asset_id"))).toString();auto asset=one(db,"SELECT id,asset_code AS code,name,location,model FROM assets WHERE id=? AND active=1",{assetId});
    QVariantList defects,checks,photos,events,reports;QSqlQuery q(db);
    q.prepare("SELECT d.* FROM defects d WHERE d.asset_id=? AND (?='' OR d.record_id=?) AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='defect' AND z.entity_id=d.id AND z.restored_at='') ORDER BY d.created_at");q.addBindValue(assetId);q.addBindValue(recordId);q.addBindValue(recordId);q.exec();while(q.next()){auto d=row(q);if(access.defect(d["id"].toString()))defects<<d;}
    if(!recordId.isEmpty()){
        q.prepare("SELECT * FROM inspection_check_items WHERE record_id=? ORDER BY rowid");q.addBindValue(recordId);q.exec();while(q.next())checks<<row(q);
        q.prepare("SELECT id,path,created_at FROM attachments WHERE record_id=? ORDER BY created_at");q.addBindValue(recordId);q.exec();while(q.next()){auto photo=row(q);if(QImageReader(photo["path"].toString()).canRead())photos<<photo;}
        q.prepare("SELECT id,title,path,content,created_at FROM product_evidence WHERE record_id=? AND deleted_at='' AND path<>'' ORDER BY created_at");q.addBindValue(recordId);q.exec();while(q.next()){auto photo=row(q);if(QImageReader(photo["path"].toString()).canRead())photos<<photo;}
        q.prepare("SELECT revision,created_at,reason FROM report_versions WHERE record_id=? ORDER BY revision DESC");q.addBindValue(recordId);q.exec();while(q.next())reports<<row(q);
    }
    for(auto v:defects){auto d=v.toMap();q.prepare("SELECT * FROM defect_events WHERE defect_id=? ORDER BY rowid");q.addBindValue(d["id"]);q.exec();while(q.next()){auto e=row(q);e["defect_title"]=d["title"];events<<e;}}
    if (!selectedDefect.isEmpty() && selectedDefect.value("status") == "in_progress") {
        const auto transition = one(db, "SELECT event_type FROM defect_events WHERE defect_id=? ORDER BY rowid DESC LIMIT 1", {id});
        selectedDefect["returned"] = transition.value("event_type") == "reopen";
    }
    auto notes=entries(recordId);auto stages=flow(task,record,defects,notes);bool complete=stages.last().toMap()["state"]=="completed";
    return {{"ok",true},{"asset",asset},{"task",task},{"record",record},{"selected_defect",selectedDefect},{"checks",checks},{"defects",defects},{"events",events},{"photos",photos},{"reports",reports},{"entries",notes},{"all_entries",entries(recordId,true)},{"flow",stages},{"state",complete?"completed":"pending"},{"state_text",complete?"已完成":"待完成"},{"can_write",canWrite(recordId)}};
}
QVariantMap InspectionWorkflowService::overview(const QString &assetId)const{
    if(!AccessPolicy(db_,session_).authenticated()||assetId.isEmpty())return fail("请先选择设备");QVariantList tasks,records,defects;
    auto all=[&](const QString&entity){QVariantList list;for(int page=0;page<100;page++){auto r=workspace_->query(entity,{{"asset_id",assetId}},page,100);auto batch=r.value("rows").toList();list+=batch;if(list.size()>=r.value("total").toInt()||batch.isEmpty())break;}return list;};
    tasks=all("tasks");records=all("records");defects=all("defects");int done=0,pending=0,open=0,reviewCount=0,unfinishedTasks=0,startedTasks=0,draftRecords=0;for(auto v:tasks){auto t=v.toMap();if(t["status"]!="completed")++unfinishedTasks;if(t["status"]=="in_progress"||t["status"]=="completed")++startedTasks;}for(auto v:records)if(v.toMap()["status"]!="submitted")++draftRecords;QVariantMap states;QHash<QString,int> pendingEntries,openIssues;
    QSqlQuery process(db_->connection());process.prepare("SELECT e.record_id,e.metadata FROM product_evidence e JOIN inspection_records r ON r.id=e.record_id WHERE r.asset_id=? AND e.deleted_at='' AND e.kind='analysis'");process.addBindValue(assetId);process.exec();while(process.next()){auto m=QJsonDocument::fromJson(process.value(1).toByteArray()).object();if(m["workflow_entry"].toBool()&&m["state"].toString()!="completed")++pendingEntries[process.value(0).toString()];}
    for(auto d:defects){auto m=d.toMap();if(m["status"]!="closed")++openIssues[m["record_id"].toString()];}
    for(auto v:records){auto r=v.toMap();const auto id=r["id"].toString();int issues=openIssues.value(id),notes=pendingEntries.value(id);bool complete=r["status"]=="submitted"&&issues==0&&notes==0;states[id]=complete?"已完成":"待完成";if(complete)++done;else ++pending;}
    for(auto d:defects){if(d.toMap()["status"]!="closed")++open;if(d.toMap()["status"]=="review")++reviewCount;}
    QVariantList stages;for(auto pair:QList<QPair<QString,QString>>{{"dispatch","派发"},{"inspect","巡检"},{"record","记录"},{"remediate","整改"},{"review","复核"},{"complete","完成"}}){QString label=pair.first=="dispatch"?QString::number(tasks.size())+" 个任务":pair.first=="record"?QString::number(records.size())+" 条记录":pair.first=="remediate"?QString::number(open)+" 项未关闭":pair.first=="complete"?QString::number(done)+" 完成 / "+QString::number(pending)+" 待完成":"";stages<<QVariantMap{{"id",pair.first},{"name",pair.second},{"state",tasks.isEmpty()?"pending":pair.first=="dispatch"?"completed":pair.first=="inspect"?unfinishedTasks==0?"completed":startedTasks>0?"active":"pending":pair.first=="record"?records.isEmpty()?"pending":draftRecords==0?"completed":"active":pair.first=="complete"?unfinishedTasks==0&&pending==0&&open==0&&done>0?"completed":"pending":pair.first=="review"?pending==0&&open==0&&done>0?"completed":reviewCount>0?"active":"pending":pending==0&&open==0&&done>0?"completed":open>0||pending>0?"active":"pending"},{"detail",label}};}
    return {{"ok",true},{"flow",stages},{"record_states",states},{"completed",done},{"pending",pending},{"open_defects",open}};
}
QVariantMap InspectionWorkflowService::saveEntry(const QString &recordId,const QVariantMap &v){
    if(!canWrite(recordId))return fail("无权修改此巡检的处理过程");if(workspace_->busy())return fail("后台维护中，请稍后");auto title=text(v,"title"),content=text(v,"content"),stage=text(v,"stage"),state=text(v,"state");
    if(title.isEmpty()||title.size()>160||content.size()>50000||!QStringList{"observation","action","review","conclusion"}.contains(stage)||!QStringList{"pending","completed"}.contains(state))return fail("请填写标题、有效阶段和完成状态");
    QString id=text(v,"id");QVariantMap before,meta;int revision=0;
    if(!id.isEmpty()){for(auto n:entries(recordId,true))if(n.toMap()["id"]==id)before=n.toMap();if(before.isEmpty()||!before["deleted_at"].toString().isEmpty())return fail("过程记录不存在或已删除");revision=before["revision"].toInt();if(revision!=v.value("revision").toInt())return fail("记录已被修改，请刷新后再编辑");meta=before["metadata"].toMap();}else id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path=before.value("path",QStringLiteral("")).toString(),hash=before.value("sha256").toString(),createdFile;
    auto input=text(v,"image");if(input.startsWith("file:"))input=QUrl(input).toLocalFile();
    if(v.value("remove_image").toBool()){path=QStringLiteral("");hash.clear();}
    if(!input.isEmpty()){
        if(!workspace_->inputFiles(input,"image").value("ok").toBool())return fail("图片无效或属于其他账号");QFileInfo file(input);QImageReader reader(input);auto size=reader.size();if(file.size()>8*1024*1024||!reader.canRead()||!size.isValid()||qint64(size.width())*size.height()>24000000)return fail("请选择小于 8 MB、2400 万像素的图片");
        QString folder=QFileInfo(db_->databasePath()).absolutePath()+"/evidence/workflow/"+recordId;QDir().mkpath(folder);path=folder+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+file.suffix().toLower();QFile source(input);QSaveFile target(path);if(!source.open(QIODevice::ReadOnly)||!target.open(QIODevice::WriteOnly))return fail("图片无法保存");auto bytes=source.readAll();hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());if(target.write(bytes)!=bytes.size()||!target.commit())return fail("图片保存不完整");QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);createdFile=path;
    }
    meta["workflow_entry"]=true;meta["stage"]=stage;meta["state"]=state;meta["revision"]=revision+1;meta["updated_at"]=now();meta["updated_by"]=session_->userId();
    if(path.isEmpty())hash=QString::fromLatin1(QCryptographicHash::hash((content+json(meta)).toUtf8(),QCryptographicHash::Sha256).toHex());
    auto db=db_->connection();if(!db.transaction()){if(!createdFile.isEmpty())QFile::remove(createdFile);return fail("数据库暂时不可写");}QSqlQuery q(db);auto reject=[&]{db.rollback();if(!createdFile.isEmpty())QFile::remove(createdFile);return fail(q.lastError().text().isEmpty()?"过程记录保存失败":q.lastError().text());};
    if(before.isEmpty()){q.prepare("INSERT INTO product_evidence(id,record_id,kind,title,path,sha256,content,metadata,created_by,created_at) VALUES(?,?,'analysis',?,?,?,?,?,?,?)");for(auto a:QVariantList{id,recordId,title,path,hash,content,json(meta),session_->userId(),now()})q.addBindValue(a);}
    else{q.prepare("UPDATE product_evidence SET title=?,path=?,sha256=?,content=?,metadata=? WHERE id=? AND deleted_at=''");for(auto a:QVariantList{title,path,hash,content,json(meta),id})q.addBindValue(a);}
    if(!q.exec())return reject();QString error;if(!db_->appendAudit(before.isEmpty()?"create_process":"update_process","inspection_record",recordId,json(QVariantMap{{"entry_id",id},{"before",before},{"after",QVariantMap{{"title",title},{"content",content},{"path",path},{"sha256",hash},{"metadata",meta}}}}),&error)){db.rollback();if(!createdFile.isEmpty())QFile::remove(createdFile);return fail(error);}if(!db.commit())return reject();emit changed();emit workspace_->dataChanged();return {{"ok",true},{"id",id},{"revision",revision+1}};
}
QVariantMap InspectionWorkflowService::setEntryDeleted(const QString &id,bool deleted,int revision){
    auto db=db_->connection();auto raw=one(db,"SELECT * FROM product_evidence WHERE id=?",{id});auto meta=QJsonDocument::fromJson(raw.value("metadata").toByteArray()).object().toVariantMap();if(!meta.value("workflow_entry").toBool()||!canWrite(raw.value("record_id").toString()))return fail("过程记录不存在或无操作权限");if(meta.value("revision",1).toInt()!=revision)return fail("记录已被修改，请刷新");
    if(workspace_->busy()||!db.transaction())return fail("数据库暂时不可写");meta["revision"]=revision+1;meta["updated_at"]=now();meta["updated_by"]=session_->userId();QSqlQuery q(db);q.prepare("UPDATE product_evidence SET sha256=?,deleted_at=?,metadata=? WHERE id=?");q.addBindValue(raw["path"].toString().isEmpty()?QString::fromLatin1(QCryptographicHash::hash((raw["content"].toString()+json(meta)).toUtf8(),QCryptographicHash::Sha256).toHex()):raw["sha256"].toString());q.addBindValue(deleted?now():QStringLiteral(""));q.addBindValue(json(meta));q.addBindValue(id);QString error;if(!q.exec()||!db_->appendAudit(deleted?"delete_process":"restore_process","inspection_record",raw["record_id"].toString(),json(QVariantMap{{"entry_id",id},{"before",raw},{"revision",revision+1}}),&error)||!db.commit()){db.rollback();return fail(error.isEmpty()?q.lastError().text():error);}emit changed();emit workspace_->dataChanged();return {{"ok",true}};
}
