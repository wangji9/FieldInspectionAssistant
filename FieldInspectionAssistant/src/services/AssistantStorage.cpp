#include "AssistantStorage.h"
#include "../app/SessionManager.h"
#include <QImageReader>
#include <QSaveFile>
namespace {
QVariantMap failure(const QString &s){return {{"ok",false},{"error",s}};}
QString key(const QString &s){return QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(s).hasMatch()?s:QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(),QCryptographicHash::Sha256).toHex());}
QString local(const QString &s){return s.startsWith("file:")?QUrl(s).toLocalFile():s;}
bool inside(const QString &p,const QString &base){return p==base||p.startsWith(base+"/");}
QStringList extensions(const QString &kind){if(kind=="image")return {"jpg","jpeg","png","bmp","webp"};if(kind=="audio")return {"wav","mp3","flac","m4a","ogg","aac"};return {"txt","md","csv","json","html","htm","pdf"};}
}
AssistantStorage::AssistantStorage(SessionManager *s):session_(s),root_(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/assistant"){
    writer_=QThread::create([this]{runWriter();});writer_->setObjectName("AssistantSnapshotWriter");writer_->start();
}
AssistantStorage::~AssistantStorage(){
    {QMutexLocker lock(&writeMutex_);stopping_=true;writeReady_.wakeAll();}
    writer_->wait();delete writer_;
}
void AssistantStorage::runWriter(){
    for(;;){
        WriteTask task;
        {QMutexLocker lock(&writeMutex_);
            while(writeOrder_.isEmpty()&&!stopping_)writeReady_.wait(&writeMutex_);
            if(writeOrder_.isEmpty()&&stopping_)return;
            const auto path=writeOrder_.takeFirst();task=writes_.take(path);
        }
        if(!QDir().mkpath(task.ownerRoot)||!QFile::setPermissions(task.ownerRoot,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)||!QDir().mkpath(QFileInfo(task.path).absolutePath())){
            qWarning()<<"Assistant snapshot directory could not be secured";continue;
        }
        QSaveFile file(task.path);const auto bytes=QJsonDocument::fromVariant(task.value).toJson(QJsonDocument::Indented);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
            qWarning()<<"Assistant snapshot write failed:"<<file.errorString();continue;
        }
        if(!QFile::setPermissions(task.path,QFile::ReadOwner|QFile::WriteOwner))qWarning()<<"Assistant snapshot permissions could not be restricted";
    }
}
QString AssistantStorage::userRoot()const{return root_+"/users/"+key(session_->userId());}
QString AssistantStorage::sessionRoot(const QString &id)const{return userRoot()+"/conversations/"+key(id);}
QVariantMap AssistantStorage::paths()const{if(!session_->can("inspect"))return {};return {{"root",root_},{"user",userRoot()},{"conversations",userRoot()+"/conversations"},{"imports",userRoot()+"/imports"},{"cache",userRoot()+"/cache"}};}
bool AssistantStorage::permitted(const QString &path)const{const QString canonical=QFileInfo(path).canonicalFilePath();return !inside(canonical,root_+"/users")||inside(canonical,userRoot());}
QVariantMap AssistantStorage::files(const QString &directory,const QString &kind)const{
    if(!session_->can("inspect"))return failure("请先登录");
    if(!QStringList{"image","audio","document"}.contains(kind))return failure("文件类型无效");
    QFileInfo target(directory.trimmed().isEmpty()?QDir::homePath():local(directory.trimmed()));
    if(target.isFile()){if(!target.isReadable()||!permitted(target.absoluteFilePath())||!extensions(kind).contains(target.suffix().toLower()))return failure("文件类型不匹配或不可读取");return {{"ok",true},{"file",target.canonicalFilePath()}};}
    if(!target.isDir()||!target.isReadable()||!permitted(target.absoluteFilePath()))return failure("目录不存在、不可读取或属于其他账号");
    QDir dir(target.canonicalFilePath());QVariantList rows;int total=0;
    const auto list=dir.entryInfoList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot|QDir::Readable,QDir::DirsFirst|QDir::Name|QDir::IgnoreCase);
    for(auto file:list){if(!permitted(file.absoluteFilePath())||(!file.isDir()&&!extensions(kind).contains(file.suffix().toLower())))continue;++total;if(rows.size()<1000)rows<<QVariantMap{{"name",file.fileName()},{"path",file.absoluteFilePath()},{"directory",file.isDir()},{"bytes",file.size()}};}
    auto parent=dir;parent.cdUp();return {{"ok",true},{"directory",dir.absolutePath()},{"parent",parent.absolutePath()},{"rows",rows},{"total",total},{"truncated",total>1000}};
}
QVariantMap AssistantStorage::write(const QString &path,const QVariantMap &value)const{
    if(!session_->can("inspect"))return failure("当前账号无效");
    const auto ownerRoot=userRoot();
    {QMutexLocker lock(&writeMutex_);
        if(stopping_)return failure("智能体文件保存器已停止");
        // A single writer keeps in-flight commits ordered. Keep only the newest
        // queued snapshot per exact owner/session path and place it at the tail.
        writeOrder_.removeAll(path);writes_[path]=WriteTask{path,ownerRoot,value};writeOrder_.append(path);writeReady_.wakeOne();
    }
    return {{"ok",true},{"pending",true},{"path",path}};
}
QVariantMap AssistantStorage::snapshot(const QString &mode,const QString &id,const QVariantList &messages,const QString &model,const QStringList &tools)const{
    if(id.isEmpty())return failure("会话标识为空");return write(sessionRoot(id)+"/session.json",{{"schema",1},{"user_id",session_->userId()},{"conversation_id",id},{"mode",mode},{"model",model},{"messages",messages},{"tools",tools},{"updated_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
}
QVariantMap AssistantStorage::memory(const QString &id,const QString &mode,bool enabled,const QVariantList &history)const{
    if(id.isEmpty())return {{"ok",true}};
    return write(sessionRoot(id)+"/memory/context.json",{{"schema",1},{"user_id",session_->userId()},{"conversation_id",id},{"mode",mode},{"enabled",enabled},{"history",enabled?history:QVariantList{}},{"source","SQLite verified completed turns"},{"updated_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
}
QVariantMap AssistantStorage::preferences(const QVariantMap &memory,const QStringList &tools,const QVariantMap &search)const{return write(userRoot()+"/preferences.json",{{"schema",1},{"memory",memory},{"tools",tools},{"web_search",search}});}
QString AssistantStorage::resolve(const QString &conversation,const QString &relative)const{
    if(!session_->can("inspect")||conversation.isEmpty()||!relative.startsWith("attachments/")||relative.contains("..")||QDir::isAbsolutePath(relative))return {};
    const auto path=QFileInfo(sessionRoot(conversation)+"/"+relative).canonicalFilePath();return inside(path,sessionRoot(conversation)+"/attachments")&&QFileInfo(path).isFile()?path:QString();
}
QVariantMap AssistantStorage::stage(const QString &conversation,const QString &image,const QString &audio)const{
    if(!session_->can("inspect")||conversation.isEmpty())return failure("会话或账号无效");QVariantList rows;QStringList created;
    auto reject=[&](const QString &s){for(auto p:created)QFile::remove(p);return failure(s);};
    for(auto kind:QStringList{"image","audio"}){
        auto path=local(kind=="image"?image:audio);if(path.isEmpty())continue;QFileInfo info(path);
        if(!info.isFile()||!info.isReadable()||!permitted(path)||info.size()<=0||info.size()>32*1024*1024||!extensions(kind).contains(info.suffix().toLower()))return reject("请选择可读取且不超过 32 MB 的"+QString(kind=="image"?"图片":"音频"));
        if(kind=="image"){QImageReader reader(path);auto size=reader.size();if(!reader.canRead()||size.width()<=0||qint64(size.width())*size.height()>64000000)return reject("图片格式无效或超过 6400 万像素");}
        QString relative="attachments/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+info.suffix().toLower();QString target=sessionRoot(conversation)+"/"+relative;
        if(!QDir().mkpath(QFileInfo(target).absolutePath()))return reject("无法创建附件目录");
        QFile input(path);QSaveFile output(target);QCryptographicHash hash(QCryptographicHash::Sha256);qint64 copied=0;
        if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))return reject("附件无法读取或保存");
        while(!input.atEnd()){auto block=input.read(1024*1024);if(block.isEmpty()||output.write(block)!=block.size())return reject("附件复制失败");hash.addData(block);copied+=block.size();}
        if(copied!=info.size()||!output.commit())return reject("附件保存不完整");created<<target;QFile::setPermissions(target,QFile::ReadOwner|QFile::WriteOwner);
        rows<<QVariantMap{{"kind",kind},{"name",info.fileName()},{"relative_path",relative},{"path",target},{"sha256",QString::fromLatin1(hash.result().toHex())},{"bytes",copied}};
    }
    QFile::setPermissions(userRoot(),QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);return {{"ok",true},{"attachments",rows}};
}
