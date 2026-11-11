#include "RecordingCatalog.h"
#include "../database/DatabaseManager.h"
#include "../app/SessionManager.h"

namespace {
QVariantMap fail(const QString &text){return {{"ok",false},{"error",text}};}
QVariantMap values(QSqlQuery &q){QVariantMap r;auto fields=q.record();for(int i=0;i<fields.count();++i)r[fields.fieldName(i)]=q.value(i);return r;}
QString stamp(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);}
QString json(const QVariantMap &m){return QString::fromUtf8(QJsonDocument::fromVariant(m).toJson(QJsonDocument::Compact));}
bool references(const QJsonValue &v,const QString &id){
    if(v.isString())return v.toString()==id;
    if(v.isArray()){for(auto child:v.toArray())if(references(child,id))return true;}
    if(v.isObject()){const auto object=v.toObject();for(auto it=object.begin();it!=object.end();++it)if(references(it.value(),id))return true;}
    return false;
}
}
RecordingCatalog::RecordingCatalog(DatabaseManager *business,SessionManager *session,const QString &folder):business_(business),session_(session){
    QDir().mkpath(folder);connection_=QStringLiteral("recordings_%1").arg(reinterpret_cast<quintptr>(this));
    db_=QSqlDatabase::addDatabase("QSQLITE",connection_);db_.setDatabaseName(folder+"/measurements.sqlite");
    if(!db_.open()){error_=db_.lastError().text();return;}
    auto execute=[&](const QString &sql){QSqlQuery q(db_);if(!q.exec(sql)){error_=q.lastError().text();return false;}return true;};
    execute("PRAGMA busy_timeout=5000");execute("PRAGMA journal_mode=WAL");
    if(!db_.transaction()){error_=db_.lastError().text();return;}
    const QStringList statements={
        "CREATE TABLE IF NOT EXISTS recordings(id TEXT PRIMARY KEY,path TEXT NOT NULL,source TEXT NOT NULL,fs INTEGER NOT NULL,channels INTEGER NOT NULL,started REAL NOT NULL,duration REAL NOT NULL DEFAULT 0,origin TEXT NOT NULL,complete INTEGER NOT NULL DEFAULT 0,calibration TEXT NOT NULL DEFAULT '{}')",
        "CREATE TABLE IF NOT EXISTS measurements(id INTEGER PRIMARY KEY,recording_id TEXT NOT NULL,channel INTEGER NOT NULL,time REAL NOT NULL,metrics TEXT NOT NULL)",
        "CREATE INDEX IF NOT EXISTS measurements_time ON measurements(recording_id,channel,time)",
        "CREATE TABLE IF NOT EXISTS recording_events(id INTEGER PRIMARY KEY,recording_id TEXT NOT NULL,action TEXT NOT NULL,actor_id TEXT NOT NULL,at TEXT NOT NULL,before_json TEXT NOT NULL,after_json TEXT NOT NULL)"};
    for(auto sql:statements)if(!execute(sql)){db_.rollback();return;}
    QSet<QString> columns;QSqlQuery info(db_);info.exec("PRAGMA table_info(recordings)");while(info.next())columns.insert(info.value(1).toString());
    const QList<QPair<QString,QString>> additions={{"name","TEXT NOT NULL DEFAULT ''"},{"notes","TEXT NOT NULL DEFAULT ''"},{"owner_id","TEXT NOT NULL DEFAULT ''"},{"deleted_at","TEXT NOT NULL DEFAULT ''"},{"updated_at","TEXT NOT NULL DEFAULT ''"},{"updated_by","TEXT NOT NULL DEFAULT ''"},{"revision","INTEGER NOT NULL DEFAULT 0"}};
    for(auto field:additions)if(!columns.contains(field.first)&&!execute("ALTER TABLE recordings ADD COLUMN "+field.first+" "+field.second)){db_.rollback();return;}
    if(!execute("CREATE INDEX IF NOT EXISTS recording_catalog_order ON recordings(deleted_at,started DESC,id)")||!execute("PRAGMA user_version=2")||!db_.commit()){if(error_.isEmpty())error_=db_.lastError().text();db_.rollback();}
}
RecordingCatalog::~RecordingCatalog(){db_.close();db_=QSqlDatabase();QSqlDatabase::removeDatabase(connection_);}
bool RecordingCatalog::manager()const{return session_->can("assign");}
bool RecordingCatalog::writable(const QVariantMap &r)const{return session_->can("inspect")&&(manager()||(!r.value("owner_id").toString().isEmpty()&&r.value("owner_id").toString()==session_->userId()));}
QVariantMap RecordingCatalog::row(const QString &id)const{QSqlQuery q(db_);q.prepare("SELECT * FROM recordings WHERE id=?");q.addBindValue(id);if(!q.exec()||!q.next())return {};return values(q);}
QVariantMap RecordingCatalog::decorate(QVariantMap r)const{
    const auto path=r.value("path").toString();QFileInfo file(path);r["file_exists"]=file.isFile();r["bytes"]=file.isFile()?file.size():0;
    r["display_name"]=r.value("name").toString().isEmpty()?r.value("source"):r.value("name");r["can_edit"]=writable(r);return r;
}
QVariantMap RecordingCatalog::query(const QVariantMap &filter,int page,int size)const{
    if(!ready())return fail(error_);if(!session_->can("inspect"))return fail("请登录后查看录音");
    QString where=" WHERE deleted_at"+QString(filter.value("trash").toBool()?"<>''":"=''");QVariantList args;
    if(filter.value("trash").toBool()&&!manager()){where+=" AND owner_id=?";args<<session_->userId();}
    auto search=filter.value("search").toString().trimmed().left(200);
    if(!search.isEmpty()){where+=" AND instr(lower(name||' '||source||' '||notes||' '||id||' '||strftime('%Y-%m-%d %H:%M:%S',started,'unixepoch','localtime')),lower(?))>0";args<<search;}
    auto origin=filter.value("origin").toString();if(origin=="sensor"||origin=="imported"){where+=" AND origin=?";args<<origin;}
    QSqlQuery count(db_);count.prepare("SELECT COUNT(*) FROM recordings"+where);for(auto a:args)count.addBindValue(a);
    if(!count.exec()||!count.next())return fail(count.lastError().text());int total=count.value(0).toInt();size=qBound(1,size,100);page=qBound(0,page,qMax(0,(total-1)/size));
    QSqlQuery q(db_);q.prepare("SELECT * FROM recordings"+where+" ORDER BY started DESC,id LIMIT ? OFFSET ?");for(auto a:args)q.addBindValue(a);q.addBindValue(size);q.addBindValue(page*size);
    if(!q.exec())return fail(q.lastError().text());QVariantList list;while(q.next())list<<decorate(values(q));
    return {{"ok",true},{"rows",list},{"total",total},{"page",page},{"size",size}};
}
QVariantMap RecordingCatalog::detail(const QString &id)const{
    if(!session_->can("inspect"))return fail("请登录后查看录音");auto r=row(id);if(r.isEmpty())return fail("录音不存在");
    if(!r.value("deleted_at").toString().isEmpty()&&!writable(r))return fail("没有查看此回收记录的权限");
    QVariantList events;QSqlQuery q(db_);q.prepare("SELECT action,actor_id,at FROM recording_events WHERE recording_id=? ORDER BY id DESC LIMIT 30");q.addBindValue(id);if(q.exec())while(q.next())events<<values(q);
    return {{"ok",true},{"record",decorate(r)},{"events",events}};
}
bool RecordingCatalog::event(const QString &id,const QString &action,const QVariantMap &before,const QVariantMap &after){
    QSqlQuery q(db_);q.prepare("INSERT INTO recording_events(recording_id,action,actor_id,at,before_json,after_json) VALUES(?,?,?,?,?,?)");
    for(auto v:QVariantList{id,action,session_->userId(),stamp(),json(before),json(after)})q.addBindValue(v);return q.exec();
}
QVariantMap RecordingCatalog::update(const QString &id,const QString &name,const QString &notes,int revision){
    if(!ready())return fail(error_);auto old=row(id);if(old.isEmpty())return fail("录音不存在");if(!writable(old))return fail("只能修改本人录音；历史公共录音由主管或管理员维护");
    if(!old.value("deleted_at").toString().isEmpty())return fail("请先恢复录音再编辑");
    auto title=name.trimmed();if(title.isEmpty()||title.size()>120||notes.size()>4000)return fail("名称需为 1–120 字，备注不能超过 4000 字");
    if(!db_.transaction())return fail(db_.lastError().text());QSqlQuery q(db_);q.prepare("UPDATE recordings SET name=?,notes=?,updated_at=?,updated_by=?,revision=revision+1 WHERE id=? AND revision=? AND deleted_at=''");
    for(auto v:QVariantList{title,notes,stamp(),session_->userId(),id,revision})q.addBindValue(v);
    if(!q.exec()||q.numRowsAffected()!=1){db_.rollback();return fail("记录已被修改，请刷新后重试");}
    if(!event(id,"update",old,row(id))||!db_.commit()){db_.rollback();return fail("修改未保存，数据库写入失败");}return {{"ok",true},{"id",id}};
}
bool RecordingCatalog::linked(const QString &id,const QString &path,QString *error)const{
    const auto acoustic=QFileInfo(business_->databasePath()).absolutePath()+"/acoustic/evaluations.sqlite";
    if(QFile::exists(acoustic)){
        const auto name="acoustic_link_"+QUuid::createUuid().toString(QUuid::WithoutBraces);bool found=false,failed=false;
        {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(acoustic);db.setConnectOptions("QSQLITE_OPEN_READONLY");if(!db.open())failed=true;else{QSqlQuery q(db);q.prepare("SELECT 1 FROM sources WHERE recording_id=? OR path=? LIMIT 1");q.addBindValue(id);q.addBindValue(path);if(!q.exec())failed=true;else found=q.next();}db.close();}
        QSqlDatabase::removeDatabase(name);if(failed){*error=QStringLiteral("无法核对声学报告证据关联，暂不允许删除");return true;}if(found){*error=QStringLiteral("录音已关联声学评价报告，不能删除");return true;}
    }
    QSqlQuery evidence(business_->connection());
    if(!evidence.exec("SELECT path,metadata FROM product_evidence")){*error="无法校验巡检证据关联，暂不允许删除";return true;}
    while(evidence.next())if(evidence.value(0).toString()==path||references(QJsonDocument::fromJson(evidence.value(1).toByteArray()).object(),id))return true;
    QSqlQuery attachment(business_->connection());attachment.prepare("SELECT 1 FROM attachments WHERE path=? LIMIT 1");attachment.addBindValue(path);
    if(!attachment.exec()){*error="无法校验附件关联，暂不允许删除";return true;}return attachment.next();
}
QVariantMap RecordingCatalog::setDeleted(const QString &id,bool deleted,int revision){
    if(!ready())return fail(error_);auto old=row(id);if(old.isEmpty())return fail("录音不存在");if(!writable(old))return fail("只能维护本人录音；历史公共录音由主管或管理员维护");
    if(old.value("revision").toInt()!=revision)return fail("记录已被修改，请刷新后重试");
    if(old.value("deleted_at").toString().isEmpty()!=deleted)return fail(deleted?"录音已在回收站":"录音已经恢复");
    if(deleted){QString error;if(linked(id,old.value("path").toString(),&error))return fail(error.isEmpty()?"录音已关联巡检证据，不能删除":error);}
    if(!db_.transaction())return fail(db_.lastError().text());QSqlQuery q(db_);q.prepare("UPDATE recordings SET deleted_at=?,updated_at=?,updated_by=?,revision=revision+1 WHERE id=? AND revision=?");
    for(auto v:QVariantList{deleted?stamp():QStringLiteral(""),stamp(),session_->userId(),id,revision})q.addBindValue(v);
    if(!q.exec()||q.numRowsAffected()!=1||!event(id,deleted?"delete":"restore",old,row(id))||!db_.commit()){db_.rollback();return fail("操作未保存，请刷新后重试");}return {{"ok",true},{"id",id}};
}
QVariantMap RecordingCatalog::beginCapture(const QVariantMap &r){
    if(!ready())return fail(error_);if(!session_->can("inspect"))return fail("需要巡检权限");
    if(!db_.transaction())return fail(db_.lastError().text());QSqlQuery q(db_);q.prepare("INSERT INTO recordings(id,path,source,fs,channels,started,origin,owner_id) VALUES(?,?,?,?,?,?,'sensor',?)");
    for(auto v:QVariantList{r.value("id"),r.value("path"),r.value("source"),r.value("fs"),r.value("channels"),QDateTime::currentMSecsSinceEpoch()/1000.,session_->userId()})q.addBindValue(v);
    if(!q.exec()||!event(r.value("id").toString(),"capture",{},row(r.value("id").toString()))||!db_.commit()){db_.rollback();return fail("采集记录建立失败，请检查数据库");}return {{"ok",true}};
}
