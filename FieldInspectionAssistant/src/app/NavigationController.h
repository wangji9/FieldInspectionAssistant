#pragma once

#include <QtCore>

// 页面导航：单窗口工作台内部页面切换，支持携带参数（如巡检任务）。
class NavigationController final : public QObject {
    Q_OBJECT
public:
    explicit NavigationController(QObject *parent = nullptr);

    Q_PROPERTY(QString page READ page NOTIFY pageChanged)
    QString page() const { return page_; }

    Q_INVOKABLE void push(const QString &page, const QVariantMap &params = QVariantMap());
    Q_INVOKABLE void pop();
    Q_INVOKABLE QVariant param(const QString &key) const;
    Q_INVOKABLE bool hasParam(const QString &key) const;

signals:
    void pageChanged();

private:
    QString page_ = QStringLiteral("dashboard");
    QVariantMap params_;
};
