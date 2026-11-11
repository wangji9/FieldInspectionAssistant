#include "NavigationController.h"

NavigationController::NavigationController(QObject *parent) : QObject(parent) {}

void NavigationController::push(const QString &page, const QVariantMap &params) {
    const QMap<QString,QString> merged{{"tasks","tasks"},{"devices","device"},{"defects","defects"},{"history","records"}};
    if(merged.contains(page)){auto next=params;next["view"]=merged.value(page);push("operations",next);return;}
    if(page=="users"){auto next=params;next["section"]="users";push("settings",next);return;}
    if (page_ == page && params_.isEmpty() && params.isEmpty()) return;
    page_ = page;
    qInfo().noquote() << "Navigate:" << page;
    params_ = params;
    emit pageChanged();
}

void NavigationController::pop() {
    page_ = QStringLiteral("dashboard");
    params_.clear();
    emit pageChanged();
}

QVariant NavigationController::param(const QString &key) const {
    return params_.value(key);
}

bool NavigationController::hasParam(const QString &key) const {
    return params_.contains(key);
}
