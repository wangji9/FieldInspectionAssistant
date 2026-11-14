#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRect>
#include <QSocketNotifier>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <array>

class ThermalCapture : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY changed)
    Q_PROPERTY(qint64 capturedMs READ capturedMs NOTIFY changed)
    Q_PROPERTY(double fps READ fps NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString autoSaveStatus READ autoSaveStatus NOTIFY changed)
    Q_PROPERTY(QString device READ device NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString tool MEMBER tool_ NOTIFY toolChanged)
    Q_PROPERTY(bool matrix READ matrix WRITE setMatrix)

public:
    explicit ThermalCapture(QQuickItem *parent = nullptr);
    ~ThermalCapture() override;
    void paint(QPainter *painter) override;
    bool connected() const { return fd_ >= 0; }
    bool paused() const { return paused_; }
    bool hasFrame() const { return !raw_.isEmpty(); }
    qint64 capturedMs() const { return hasFrame() && lastFrame_.isValid() ? capturedMs_ : 0; }
    double fps() const { return fps_; }
    QString status() const { return status_; }
    QString autoSaveStatus() const { return autoSaveStatus_; }
    QString device() const { return device_; }
    QVariantMap stats() const { return stats_; }
    QVariantMap options() const { return options_; }
    bool matrix() const { return matrix_; }
    void setMatrix(bool value);

    struct Reading {
        qint64 time;
        std::array<double, 12> value;
    };
    const QList<Reading> &history() const { return history_; }
    static const QStringList &seriesNames();

    Q_INVOKABLE QVariantList ports() const;
    Q_INVOKABLE void start(const QString &port, bool testOnly = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void clearTrend();
    Q_INVOKABLE void clearMarks();
    Q_INVOKABLE void fit();
    Q_INVOKABLE void setZoom(double factor);
    Q_INVOKABLE QVariantMap defaults() const;
    Q_INVOKABLE bool applyOptions(const QVariantMap &options, bool persist);
    Q_INVOKABLE QString saveFrame(const QUrl &folder);
    Q_INVOKABLE QStringList saveFrameFiles(const QUrl &folder);
    Q_INVOKABLE QStringList saveInspectionFrame(const QString &folder);
    Q_INVOKABLE QString exportTrend(const QUrl &folder);

signals:
    void changed();
    void historyChanged();
    void optionsChanged();
    void toolChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QStringList saveFrameFiles(const QUrl &folder, bool png, bool raw, bool csv);
    void receive();
    void frame(const QByteArray &packet);
    void refreshMeasurements();
    void rebuildImage();
    void disconnectPort(const QString &message);
    QRectF imageRect(const QRectF &bounds) const;
    void drawImage(QPainter &painter, const QRectF &bounds, bool fitted) const;
    QPoint sensorPoint(const QPointF &position) const;
    double temperature(int x, int y) const;
    QImage image_{80, 60, QImage::Format_RGB32};
    QImage gray_{80, 60, QImage::Format_RGB32};
    std::array<int, 4800> values_{};
    QByteArray buffer_, raw_;
    QList<QPoint> points_;
    QRect roi_;
    QPoint startPoint_, minPoint_, maxPoint_;
    QPointF press_, pan_, oldPan_;
    QList<Reading> history_;
    QVariantMap stats_, options_;
    QString tool_ = "point", status_ = tr("热像仪未连接"), device_;
    QString autoSaveStatus_;
    QSocketNotifier *notifier_ = nullptr;
    QTimer ticker_;
    QElapsedTimer rate_, lastFrame_, autoSaveClock_;
    qint64 capturedMs_ = 0;
    double zoom_ = 1, fps_ = 0;
    int fd_ = -1, frames_ = 0, deviceId_ = 0;
    bool paused_ = false, matrix_ = false, testing_ = false, streaming_ = false;
};

class ThermalTrend : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(ThermalCapture *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QStringList series READ series WRITE setSeries NOTIFY changed)
    Q_PROPERTY(int seconds READ seconds WRITE setSeconds NOTIFY changed)
public:
    using QQuickPaintedItem::QQuickPaintedItem;
    ThermalCapture *source() const { return source_; }
    QStringList series() const { return series_; }
    int seconds() const { return seconds_; }
    void setSource(ThermalCapture *source);
    void setSeries(const QStringList &series);
    void setSeconds(int seconds);
    void paint(QPainter *painter) override;
signals:
    void changed();
private:
    QPointer<ThermalCapture> source_;
    QMetaObject::Connection historyConnection_;
    QStringList series_;
    int seconds_ = 60;
};
