#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <QQuickItem>
#include <QTimer>
#include <xcb/xcb.h>

class ThermalWindow : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool embedded READ embedded NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int minimumWidth READ minimumWidth NOTIFY minimumSizeChanged)
    Q_PROPERTY(int minimumHeight READ minimumHeight NOTIFY minimumSizeChanged)
    Q_PROPERTY(int contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal scrollX READ scrollX WRITE setScrollX)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY)

public:
    explicit ThermalWindow(QQuickItem *parent = nullptr);
    ~ThermalWindow() override;

    bool active() const { return process_.state() != QProcess::NotRunning; }
    bool embedded() const { return container_ != XCB_WINDOW_NONE; }
    QString status() const { return status_; }
    int minimumWidth() const { return minimum_.width(); }
    int minimumHeight() const { return minimum_.height(); }
    int contentWidth() const { return qMax(qRound(width()), minimum_.width()); }
    int contentHeight() const { return qMax(qRound(height()), minimum_.height()); }
    qreal scrollX() const { return scrollX_; }
    qreal scrollY() const { return scrollY_; }
    void setScrollX(qreal offset);
    void setScrollY(qreal offset);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    void statusChanged();
    void minimumSizeChanged();
    void contentSizeChanged();

protected:
    void geometryChange(const QRectF &next, const QRectF &previous) override;

private:
    xcb_atom_t atom(const QByteArray &name) const;
    QByteArray property(xcb_window_t window, xcb_atom_t name, xcb_atom_t type) const;
    QList<xcb_window_t> clientWindows() const;
    void findWindow();
    void embedWindow();
    void updateGeometry();
    void requestClose();
    void clearWindow();
    void setStatus(const QString &status);

    QProcess process_;
    QTimer poll_;
    QElapsedTimer startup_;
    xcb_connection_t *x11_ = nullptr;
    xcb_window_t root_ = XCB_WINDOW_NONE;
    xcb_window_t guest_ = XCB_WINDOW_NONE;
    xcb_window_t container_ = XCB_WINDOW_NONE;
    QSize minimum_{0, 0};
    qreal scrollX_ = 0;
    qreal scrollY_ = 0;
    bool stopping_ = false;
    QString status_;
};
