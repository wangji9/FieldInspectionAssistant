#pragma once

#include <QQuickPaintedItem>
#include <QVariant>

class SpeakerTimelineItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples WRITE setSamples NOTIFY dataChanged)
    Q_PROPERTY(QVariantList segments READ segments WRITE setSegments NOTIFY dataChanged)
    Q_PROPERTY(QVariantList profiles READ profiles WRITE setProfiles NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY dataChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY dataChanged)
    Q_PROPERTY(int chartHeight READ chartHeight NOTIFY dataChanged)
public:
    explicit SpeakerTimelineItem(QQuickItem *parent = nullptr);
    QVariantList samples() const { return samples_; }
    QVariantList segments() const { return segments_; }
    QVariantList profiles() const { return profiles_; }
    QVariantMap metrics() const { return metrics_; }
    int selectedIndex() const { return selectedIndex_; }
    int chartHeight() const;
    void setSamples(const QVariantList &value);
    void setSegments(const QVariantList &value);
    void setProfiles(const QVariantList &value);
    void setMetrics(const QVariantMap &value);
    void setSelectedIndex(int value);
    Q_INVOKABLE int segmentAt(qreal x) const;
    void paint(QPainter *painter) override;

signals:
    void dataChanged();

private:
    struct Lane { QString key, label; QColor color; };
    void rebuildLanes();
    void refresh();
    QVariantList samples_, segments_, profiles_;
    QVariantMap metrics_;
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
