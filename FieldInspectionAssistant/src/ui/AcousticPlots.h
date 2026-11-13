#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QVariant>

class SpeakerTimelineItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples WRITE setSamples NOTIFY dataChanged)
    Q_PROPERTY(QVariantList segments READ segments WRITE setSegments NOTIFY dataChanged)
    Q_PROPERTY(QVariantList profiles READ profiles WRITE setProfiles NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY dataChanged)
    Q_PROPERTY(QImage spectrogram READ spectrogram WRITE setSpectrogram NOTIFY dataChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY dataChanged)
    Q_PROPERTY(int chartHeight READ chartHeight NOTIFY dataChanged)
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY dataChanged)
    Q_PROPERTY(double viewStart READ viewStart WRITE setViewStart NOTIFY dataChanged)
    Q_PROPERTY(double viewSpan READ viewSpan WRITE setViewSpan NOTIFY dataChanged)
    Q_PROPERTY(int axesRevision READ axesRevision NOTIFY lanesChanged)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY dataChanged)
public:
    explicit SpeakerTimelineItem(QQuickItem *parent = nullptr);
    QVariantList samples() const { return samples_; }
    QVariantList segments() const { return segments_; }
    QVariantList profiles() const { return profiles_; }
    QVariantMap metrics() const { return metrics_; }
    QImage spectrogram() const { return spectrogram_; }
    int selectedIndex() const { return selectedIndex_; }
    int chartHeight() const;
    QString profileId() const { return profileId_; }
    double viewStart() const { return viewStart_; }
    double viewSpan() const { return viewSpan_; }
    int axesRevision() const { return axesRevision_; }
    qreal viewportHeight() const { return viewportHeight_; }
    void setSamples(const QVariantList &value);
    void setSegments(const QVariantList &value);
    void setProfiles(const QVariantList &value);
    void setMetrics(const QVariantMap &value);
    void setSpectrogram(const QImage &value);
    void setSelectedIndex(int value);
    void setProfileId(const QString &value);
    void setViewStart(double value);
    void setViewSpan(double value);
    void setViewportHeight(qreal value);
    Q_INVOKABLE int segmentAt(qreal x, qreal y) const;
    Q_INVOKABLE QColor segmentColor(int index) const;
    Q_INVOKABLE int segmentPosition(int index) const;
    void paint(QPainter *painter) override;

signals:
    void dataChanged();
    void lanesChanged();

private:
    struct Lane { QString key, label; QColor color; };
    void rebuildLanes();
    void refresh();
    qreal extraPlotHeight() const;
    QVariantList samples_, segments_, profiles_;
    QVariantMap metrics_;
    QImage spectrogram_;
    QString profileId_;
    double viewStart_ = 0., viewSpan_ = 1.;
    qreal viewportHeight_ = 0.;
    int axesRevision_ = 0;
    QList<Lane> lanes_;
    int selectedIndex_ = -1;
};

class MicrophoneArrayItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList positions READ positions WRITE setPositions NOTIFY dataChanged)
    Q_PROPERTY(QVariantList levels READ levels WRITE setLevels NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap observation READ observation WRITE setObservation NOTIFY dataChanged)
public:
    explicit MicrophoneArrayItem(QQuickItem *parent = nullptr);
    QVariantList positions() const { return positions_; }
    QVariantList levels() const { return levels_; }
    QVariantMap observation() const { return observation_; }
    void setPositions(const QVariantList &value);
    void setLevels(const QVariantList &value);
    void setObservation(const QVariantMap &value);
    void paint(QPainter *painter) override;

signals:
    void dataChanged();

private:
    QVariantList positions_, levels_;
    QVariantMap observation_;
};
