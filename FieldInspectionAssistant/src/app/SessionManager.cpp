#include "SessionManager.h"
#include "../database/DatabaseManager.h"
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <cstring>

namespace {
QByteArray randomBytes(int size) {
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i=0;i<size;i+=4) {
        const auto value=QRandomGenerator::system()->generate();
        std::memcpy(bytes.data()+i,&value,4);
    }
    return bytes;
}
QByteArray passwordHash(const QString &password,const QByteArray &salt,int iterations=600000) { return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,password.toUtf8(),salt,iterations,32); }
bool sameHash(const QByteArray &a,const QByteArray &b) { if(a.size()!=b.size())return false;unsigned char difference=0;for(int i=0;i<a.size();++i)difference|=static_cast<unsigned char>(a[i]^b[i]);return difference==0; }
QVariantMap failure(const QString &message){return {{"ok",false},{"error",message}};}
const auto LocalLoginKey=QStringLiteral("local_login_verifier");
const auto PrivatePermissions=QFileDevice::ReadOwner|QFileDevice::WriteOwner;
QString tokenPath(const DatabaseManager *db) {
    return QFileInfo(db->databasePath()).absoluteDir().filePath("maintenance/local-session");
}
QByteArray verifier(const QString &id,const QByteArray &token,const QByteArray &passwordHash) {
    return QCryptographicHash::hash(id.toUtf8()+token+passwordHash,QCryptographicHash::Sha256).toHex();
}
}

SessionManager::SessionManager(DatabaseManager *db, QObject *parent)
    : QObject(parent), db_(db) {}

QVariantList SessionManager::users() const {
    QVariantList out;
    if(!loggedIn())return out;
    const auto users = db_->loadUsers();
    for (const UserInfo &u : users) {
        if(!can("assign")&&u.id!=userId_)continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), u.id);
        m.insert(QStringLiteral("name"), u.name);
        m.insert(QStringLiteral("role"), u.role);
        m.insert(QStringLiteral("active"), u.active);
        QSqlQuery query(db_->connection());query.prepare("SELECT password_hash<>'' FROM users WHERE id=?");query.addBindValue(u.id);query.exec();m.insert("configured",query.next()&&query.value(0).toBool());
        out.push_back(m);
    }
    return out;
}

bool SessionManager::login(const QString &name) {
    // Legacy account picker cannot bypass password authentication.
    if(loggedIn()&&name==userName_)return true;
    message_=QStringLiteral("请退出当前账户，在登录页验证密码");emit sessionChanged();return false;
}

bool SessionManager::setupRequired() const {
    QSqlQuery query(db_->connection());return query.exec("SELECT COUNT(*) FROM users WHERE role='admin' AND active=1 AND password_hash<>''")&&query.next()&&query.value(0).toInt()==0;
}
void SessionManager::activate(const QString &id,const QString &name,const QString &role,const QString &auditAction) {
    userId_=id;
    userName_=name;
    role_=role;
    failures_=0;
    retryAfter_={};
    message_.clear();
    db_->setActor(id,name);
    db_->appendAudit(auditAction,"user",id,name);
    emit sessionChanged();
}

void SessionManager::forgetLocalLogin() {
    db_->setState(LocalLoginKey,QStringLiteral(""));
    QFile::remove(tokenPath(db_));
}

bool SessionManager::rememberLocalLogin(const QString &id,const QByteArray &hash) {
    forgetLocalLogin();
    const QString path=tokenPath(db_);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    const auto token=randomBytes(32).toHex();
    QSaveFile file(path);
    const auto content=id.toUtf8()+'\n'+token;
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(PrivatePermissions)
        || file.write(content)!=content.size() || !file.commit())
        return false;
    if (db_->setState(LocalLoginKey,QString::fromLatin1(verifier(id,token,hash))))
        return true;
    QFile::remove(path);
    return false;
}

void SessionManager::restoreLocalLogin() {
    QFile file(tokenPath(db_));
    const QFileInfo info(file);
    if (!info.isFile() || info.isSymLink() || info.size()>128
        || (info.permissions() & (QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ReadOther|QFileDevice::WriteOther))
        || !file.open(QIODevice::ReadOnly))
        return;
    const auto parts=file.readAll().split('\n');
    file.close();
    if (parts.size()==2 && parts[1].size()==64) {
        const auto id=QString::fromUtf8(parts[0]);
        QSqlQuery query(db_->connection());
        query.prepare("SELECT name,role,password_hash FROM users WHERE id=? AND active=1 AND password_hash<>''");
        query.addBindValue(id);
        if (query.exec() && query.next() &&
            sameHash(verifier(id,parts[1],query.value(2).toByteArray()),db_->state(LocalLoginKey).toLatin1())) {
            activate(id,query.value(0).toString(),query.value(1).toString(),"resume_login");
            return;
        }
    }
    forgetLocalLogin();
}

bool SessionManager::authenticate(const QString &name,const QString &password,bool remember) {
    if(QDateTime::currentDateTimeUtc()<retryAfter_){message_=QStringLiteral("尝试过于频繁，请稍后再试");emit sessionChanged();return false;}
    QSqlQuery query(db_->connection());query.prepare("SELECT id,name,role,active,password_hash,password_salt,password_iterations FROM users WHERE name=? AND password_hash<>''");query.addBindValue(name.trimmed());
    bool exists=query.exec()&&query.next();
    QByteArray salt=exists?QByteArray::fromHex(query.value(5).toByteArray()):QByteArray(16,'0');
    const int iterations=exists?qBound(600000,query.value(6).toInt(),2000000):600000;
    const auto hash=passwordHash(password.left(256),salt,iterations);
    bool ok=exists&&query.value(3).toBool()&&!query.value(4).toString().isEmpty()&&sameHash(hash.toHex(),query.value(4).toByteArray());
    if(!ok){++failures_;if(failures_>=5)retryAfter_=QDateTime::currentDateTimeUtc().addSecs(qMin(60,failures_*3));message_=QStringLiteral("用户名或密码错误，或账户未启用");emit sessionChanged();return false;}
    const auto id=query.value(0).toString();
    const auto username=query.value(1).toString();
    const auto role=query.value(2).toString();
    const auto credential=query.value(4).toByteArray();
    query.finish();
    if(loggedIn()&&userId_!=id)logout();
    if (remember) {
        if (!rememberLocalLogin(id,credential))
            qWarning("Could not save the local login");
    } else {
        forgetLocalLogin();
    }
    activate(id,username,role,"login");
    return true;
}
bool SessionManager::setupAdministrator(const QString &name,const QString &password,bool remember){
    if(!setupRequired()){message_=QStringLiteral("管理员已设置，请登录");emit sessionChanged();return false;}
    auto r=saveUser(QString(),name,QStringLiteral("admin"),true,password);
    if(!r.value("ok").toBool()){message_=r.value("error").toString();emit sessionChanged();return false;}
    return authenticate(name,password,remember);
}
QVariantMap SessionManager::saveUser(const QString &id,const QString &name,const QString &role,bool active,const QString &password){
    const auto username=name.trimmed();const bool setup=setupRequired()&&id.isEmpty()&&role=="admin"&&active;
    if(!setup&&!can("manage_users"))return failure(QStringLiteral("仅管理员可管理用户"));
    if(username.isEmpty()||username.size()>64)return failure(QStringLiteral("用户名为 1–64 个字符"));
    if(!QStringList{"admin","supervisor","inspector"}.contains(role))return failure(QStringLiteral("无效角色"));
    if((id.isEmpty()||!password.isEmpty())&&(password.size()<8||password.size()>256))return failure(QStringLiteral("密码需为 8–256 个字符"));
    if(id==userId_&&(!active||role!="admin"))return failure(QStringLiteral("不能停用当前管理员或移除自身管理员权限"));
    auto db=db_->connection();if(!db.transaction())return failure(db.lastError().text());
    auto reject=[&](const QString &e){db.rollback();return failure(e);};
    QSqlQuery duplicate(db);duplicate.prepare("SELECT id FROM users WHERE name=? AND id<>?");duplicate.addBindValue(username);duplicate.addBindValue(id.isEmpty()?QStringLiteral(""):id);
    if(!duplicate.exec())return reject(duplicate.lastError().text());
    if(duplicate.next())return reject(QStringLiteral("用户名已存在，请使用其他名称"));
    QSqlQuery query(db);const auto identity=id.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):id;
    if(!id.isEmpty()){
        QSqlQuery old(db);old.prepare("SELECT role,active,password_hash<>'' FROM users WHERE id=?");old.addBindValue(id);
        if(!old.exec()||!old.next())return reject(QStringLiteral("用户不存在"));
        if(old.value(0).toString()=="admin"&&old.value(1).toBool()&&old.value(2).toBool()&&(!active||role!="admin")){
            QSqlQuery count(db);count.exec("SELECT COUNT(*) FROM users WHERE role='admin' AND active=1 AND password_hash<>''");count.next();
            if(count.value(0).toInt()<=1)return reject(QStringLiteral("至少保留一位可登录的管理员"));
        }
        if(!active){QSqlQuery linked(db);linked.prepare("SELECT (SELECT COUNT(*) FROM inspection_tasks WHERE assignee_id=? AND status<>'completed')+(SELECT COUNT(*) FROM recurring_plans WHERE assignee_id=? AND enabled=1)+(SELECT COUNT(*) FROM defects WHERE assignee_id=? AND status<>'closed')");for(int i=0;i<3;i++)linked.addBindValue(id);if(!linked.exec()||!linked.next()||linked.value(0).toInt())return reject(QStringLiteral("用户仍有未完成任务、启用计划或未关闭缺陷，请先交接"));}
        query.prepare("UPDATE users SET name=?,role=?,active=? WHERE id=?");query.addBindValue(username);query.addBindValue(role);query.addBindValue(active);query.addBindValue(identity);
    }else{
        query.prepare("INSERT INTO users(id,name,role,active,created_at) VALUES(?,?,?,?,?)");query.addBindValue(identity);query.addBindValue(username);query.addBindValue(role);query.addBindValue(active);query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    }
    if(!query.exec())return reject(query.lastError().text());
    if(!password.isEmpty()){
        auto salt=randomBytes(16);query.prepare("UPDATE users SET password_hash=?,password_salt=?,password_iterations=600000 WHERE id=?");
        query.addBindValue(passwordHash(password,salt).toHex());query.addBindValue(salt.toHex());query.addBindValue(identity);
        if(!query.exec())return reject(query.lastError().text());
    }
    if(!db_->appendAudit(id.isEmpty()?"create":"update","user",identity,username+" / "+role))return reject(QStringLiteral("审计记录写入失败"));
    if(!db.commit())return failure(db.lastError().text());
    if(identity==userId_)userName_=username;
    emit usersChanged();emit sessionChanged();return {{"ok",true},{"id",identity}};
}
QVariantMap SessionManager::removeUser(const QString &id){
    if(!can("manage_users"))return failure(QStringLiteral("仅有效管理员可删除用户"));
    if(id==userId_)return failure(QStringLiteral("不能删除当前登录用户"));
    auto db=db_->connection();if(!db.transaction())return failure(db.lastError().text());
    auto reject=[&](const QString&e){db.rollback();return failure(e);};
    QSqlQuery q(db);q.prepare("SELECT role,active,name,password_hash<>'' FROM users WHERE id=?");q.addBindValue(id);
    if(!q.exec()||!q.next())return reject(QStringLiteral("用户不存在"));
    auto username=q.value(2).toString();
    if(q.value(0).toString()=="admin"&&q.value(1).toBool()&&q.value(3).toBool()){
        QSqlQuery count(db);count.exec("SELECT COUNT(*) FROM users WHERE role='admin' AND active=1 AND password_hash<>''");count.next();
        if(count.value(0).toInt()<=1)return reject(QStringLiteral("至少保留一位可登录的管理员"));
    }
    q.prepare("SELECT (SELECT COUNT(*) FROM inspection_tasks WHERE assignee_id=?)+(SELECT COUNT(*) FROM defects WHERE assignee_id=?)+(SELECT COUNT(*) FROM defect_events WHERE actor_id=?)+(SELECT COUNT(*) FROM agent_conversations WHERE user_id=?)");
    for(int i=0;i<4;i++)q.addBindValue(id);
    if(!q.exec()||!q.next())return reject(q.lastError().text());
    if(q.value(0).toInt()>0)return reject(QStringLiteral("用户关联业务记录，请改为停用以保留档案"));
    q.prepare("DELETE FROM users WHERE id=?");q.addBindValue(id);if(!q.exec())return reject(q.lastError().text());
    if(!db_->appendAudit("delete","user",id,username))return reject(QStringLiteral("审计记录写入失败"));
    if(!db.commit())return failure(db.lastError().text());emit usersChanged();return {{"ok",true}};
}

void SessionManager::logout() {
    if(loggedIn())emit sessionEnding();
    forgetLocalLogin();
    userId_.clear();
    userName_.clear();
    role_.clear();
    message_.clear();
    emit sessionChanged();
}

bool SessionManager::can(const QString &permission) const {
    if(!loggedIn())return false;
    QSqlQuery live(db_->connection());live.prepare("SELECT role FROM users WHERE id=? AND active=1 AND password_hash<>''");live.addBindValue(userId_);if(!live.exec()||!live.next())return false;
    const QString liveRole=live.value(0).toString();
    // admin: 全部；supervisor: 分派/复核；inspector: 巡检执行/登记
    if (liveRole == QLatin1String("admin")) return true;
    if (liveRole == QLatin1String("supervisor"))
        return permission == QLatin1String("assign") || permission == QLatin1String("review")
            || permission == QLatin1String("manage_assets") || permission == QLatin1String("settings")
            || permission == QLatin1String("inspect") || permission == QLatin1String("register_defect");
    if (liveRole == QLatin1String("inspector"))
        return permission == QLatin1String("inspect") || permission == QLatin1String("register_defect")
            || permission == QLatin1String("start_defect");
    return false;
}

QString SessionManager::roleText() const {
    if (role_ == QLatin1String("admin")) return QStringLiteral("系统管理员");
    if (role_ == QLatin1String("supervisor")) return QStringLiteral("主管");
    if (role_ == QLatin1String("inspector")) return QStringLiteral("巡检员");
    return QStringLiteral("未登录");
}
QVariantMap SessionManager::permissions()const{QVariantMap out;for(auto key:QStringList{"inspect","assign","review","manage_assets","manage_users","settings","register_defect","start_defect"})out[key]=can(key);return out;}
