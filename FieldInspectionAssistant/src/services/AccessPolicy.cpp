#include "AccessPolicy.h"
#include "../database/DatabaseManager.h"
#include "../app/SessionManager.h"
bool AccessPolicy::exists(const QString&sql,const QVariantList&args)const{QSqlQuery q(db_->connection());q.prepare(sql);for(auto v:args)q.addBindValue(v);return q.exec()&&q.next();}
QString AccessPolicy::user()const{return session_?session_->userId():QString();}
bool AccessPolicy::authenticated()const{return session_&&session_->loggedIn()&&exists("SELECT id FROM users WHERE id=? AND active=1 AND password_hash<>''",{user()});}
bool AccessPolicy::permission(const QString&p)const{return authenticated()&&session_->can(p);}
bool AccessPolicy::manages()const{return permission("assign")||permission("review");}
bool AccessPolicy::task(const QString&id)const{
    if(!authenticated())return false;
    QString sql="SELECT t.id FROM inspection_tasks t WHERE t.id=? AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='task' AND z.entity_id=t.id AND z.restored_at='')";QVariantList args{id};
    if(!manages()){sql+=" AND t.assignee_id=?";args<<user();}return exists(sql,args);
}
bool AccessPolicy::record(const QString&id)const{
    if(!authenticated())return false;
    QString sql="SELECT r.id FROM inspection_records r LEFT JOIN inspection_tasks t ON t.id=r.task_id WHERE r.id=? AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='record' AND z.entity_id=r.id AND z.restored_at='')";QVariantList args{id};
    if(!manages()){sql+=" AND (t.assignee_id=? OR EXISTS(SELECT 1 FROM audit_logs a WHERE a.entity='inspection_record' AND a.entity_id=r.id AND a.action='create' AND a.actor_id=?))";args<<user()<<user();}return exists(sql,args);
}
bool AccessPolicy::recordEditable(const QString&id)const{
    if(!permission("inspect"))return false;
    QString sql="SELECT r.id FROM inspection_records r LEFT JOIN inspection_tasks t ON t.id=r.task_id WHERE r.id=? AND r.status='draft' AND (COALESCE(r.task_id,'')='' OR t.status='in_progress') AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='record' AND z.entity_id=r.id AND z.restored_at='')";QVariantList args{id};
    // Handover transfers write access. A previous inspector keeps read access to authored evidence.
    if(!manages()){sql+=" AND (t.assignee_id=? OR (COALESCE(r.task_id,'')='' AND EXISTS(SELECT 1 FROM audit_logs a WHERE a.entity='inspection_record' AND a.entity_id=r.id AND a.action='create' AND a.actor_id=?)))";args<<user()<<user();}return exists(sql,args);
}
bool AccessPolicy::defect(const QString&id)const{
    if(!authenticated())return false;
    QString sql="SELECT d.id FROM defects d LEFT JOIN inspection_records r ON r.id=d.record_id LEFT JOIN inspection_tasks t ON t.id=r.task_id WHERE d.id=? AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='defect' AND z.entity_id=d.id AND z.restored_at='')";QVariantList args{id};
    if(!manages()){sql+=" AND (d.assignee_id=? OR t.assignee_id=? OR EXISTS(SELECT 1 FROM audit_logs a WHERE a.entity='defect' AND a.entity_id=d.id AND a.action='create' AND a.actor_id=?))";args<<user()<<user()<<user();}return exists(sql,args);
}
bool AccessPolicy::defectEditable(const QString&id)const{
    if(!permission("register_defect"))return false;
    QString sql="SELECT d.id FROM defects d WHERE d.id=? AND d.status<>'closed' AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='defect' AND z.entity_id=d.id AND z.restored_at='')";QVariantList args{id};
    if(!manages()){sql+=" AND d.status='unassigned' AND COALESCE(d.assignee_id,'')='' AND EXISTS(SELECT 1 FROM audit_logs a WHERE a.entity='defect' AND a.entity_id=d.id AND a.action='create' AND a.actor_id=?)";args<<user();}return exists(sql,args);
}
bool AccessPolicy::assetWork(const QString&id)const{
    if(!permission("inspect"))return false;
    QString sql="SELECT a.id FROM assets a WHERE a.id=? AND a.active=1";QVariantList args{id};
    if(!manages()){sql+=" AND (EXISTS(SELECT 1 FROM inspection_tasks t WHERE t.asset_id=a.id AND t.assignee_id=? AND t.status<>'completed' AND NOT EXISTS(SELECT 1 FROM recycle_items z WHERE z.entity='task' AND z.entity_id=t.id AND z.restored_at='')) OR EXISTS(SELECT 1 FROM defects d WHERE d.asset_id=a.id AND d.assignee_id=? AND d.status<>'closed'))";args<<user()<<user();}return exists(sql,args);
}
