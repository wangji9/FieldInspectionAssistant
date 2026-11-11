#include "NavigationController.h"

NavigationController::NavigationController(QObject *parent) : QObject(parent) {}

void NavigationController::push(const QString &page, const QVariantMap &params) {
    if (page_ == page && params_.isEmpty() && params.isEmpty()) return;
    page_ = page;
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
