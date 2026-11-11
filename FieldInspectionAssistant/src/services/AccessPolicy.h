#pragma once
#include <QtCore>
class DatabaseManager;class SessionManager;
// Every public business service uses the same role and object ownership rules.
class AccessPolicy {
public:
    AccessPolicy(DatabaseManager*d,SessionManager*s):db_(d),session_(s){}
    bool authenticated()const;
    bool permission(const QString&)const;
    bool manages()const;
    bool task(const QString&)const;
    bool record(const QString&)const;
    bool recordEditable(const QString&)const;
    bool defect(const QString&)const;
    bool defectEditable(const QString&)const;
    bool assetWork(const QString&)const;
private:
    bool exists(const QString&,const QVariantList&)const;
    QString user()const;
    DatabaseManager *db_;SessionManager *session_;
};
