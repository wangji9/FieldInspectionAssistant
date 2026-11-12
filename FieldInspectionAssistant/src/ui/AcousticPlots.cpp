#include "AcousticPlots.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr qreal PlotLeft = 101, PlotRight = 14, WaveTop = 15, WaveHeight = 54, LaneTop = 77, LaneHeight = 24, ScoreHeight = 54;
const QColor ink("#263e53"), muted("#586c7c"), grid("#e0e7ed"), axis("#aab9c6");

QString rowKey(const QVariantMap &row)
{
    return row.value("profile_id",row.value("state")).toString();
}

QString timeTick(qint64 milliseconds)
{
    const auto seconds=qRound64(milliseconds/1000.);
    return QStringLiteral("%1:%2").arg(seconds/60,2,10,QLatin1Char('0')).arg(seconds%60,2,10,QLatin1Char('0'));
}

QFont plotFont()
{
    auto font=QGuiApplication::font();
    font.setPixelSize(12);
    return font;
}
}

SpeakerTimelineItem::SpeakerTimelineItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

int SpeakerTimelineItem::chartHeight() const
{
    return int(LaneTop+std::max<qsizetype>(1,lanes_.size())*LaneHeight+13+ScoreHeight+24);
}

void SpeakerTimelineItem::setSamples(const QVariantList &value)
{
    if (samples_!=value) { samples_=value; refresh(); }
}

void SpeakerTimelineItem::setSegments(const QVariantList &value)
{
    if (segments_!=value) { segments_=value; rebuildLanes(); refresh(); }
}

void SpeakerTimelineItem::setProfiles(const QVariantList &value)
{
    if (profiles_!=value) { profiles_=value; rebuildLanes(); refresh(); }
}

void SpeakerTimelineItem::setMetrics(const QVariantMap &value)
{
    if (metrics_!=value) { metrics_=value; refresh(); }
}

void SpeakerTimelineItem::setSelectedIndex(int value)
{
    if (selectedIndex_!=value) { selectedIndex_=value; refresh(); }
}

void SpeakerTimelineItem::refresh()
{
    update();
    emit dataChanged();
}

void SpeakerTimelineItem::rebuildLanes()
{
    static const std::array<QColor,6> colors{{QColor("#3d77a2"),QColor("#478677"),QColor("#7865a1"),
        QColor("#ac7950"),QColor("#567487"),QColor("#8a597a")}};
    lanes_.clear();
    for (const auto &value : segments_) {
        const auto row=value.toMap();
        const auto key=rowKey(row);
        if (std::any_of(lanes_.cbegin(),lanes_.cend(),[&](const Lane &lane){return lane.key==key;}))
            continue;
        if (row.value("state")=="matched") {
            const auto it=std::find_if(profiles_.cbegin(),profiles_.cend(),[&](const QVariant &profile) {
                return profile.toMap().value("id")==key;
            });
            lanes_.append({key,row.value("speaker").toString(),colors[(it-profiles_.cbegin())%colors.size()]});
        } else {
            const bool unknown=row.value("state")=="unknown";
            lanes_.append({key,unknown?tr("未知发言人"):tr("语音不足"),QColor(unknown?"#8795a2":"#b9c1c9")});
        }
    }
}

int SpeakerTimelineItem::segmentAt(qreal x) const
{
    if (x<PlotLeft || x>width()-PlotRight)
        return -1;
    const auto t=metrics_.value("offset_ms").toDouble()+
        (x-PlotLeft)*metrics_.value("audio_ms").toDouble()/std::max(qreal(1),width()-PlotLeft-PlotRight);
    for (int i=0;i<segments_.size();++i) {
        const auto segment=segments_[i].toMap();
        if (t>=segment.value("start_ms").toDouble() && t<=segment.value("end_ms").toDouble())
            return i;
    }
    return -1;
}

void SpeakerTimelineItem::paint(QPainter *p)
{
    p->fillRect(boundingRect(),Qt::white);
    if (width()<PlotLeft+PlotRight+20 || samples_.isEmpty())
        return;
    p->setFont(plotFont());
    p->setRenderHint(QPainter::Antialiasing);
    const qreal plotWidth=width()-PlotLeft-PlotRight;
    const qreal offset=metrics_.value("offset_ms").toDouble();
    const qreal span=std::max(1.,metrics_.value("audio_ms").toDouble());
    const qreal waveMid=WaveTop+WaveHeight/2;
    const qreal scoreBottom=chartHeight()-24;
    const qreal scoreTop=scoreBottom-ScoreHeight;
    const auto timeX=[&](double t){return PlotLeft+(t-offset)*plotWidth/span;};
    const int ticks=std::clamp(int(plotWidth/84),2,8);
    for (int i=0;i<=ticks;++i) {
        const qreal x=PlotLeft+plotWidth*i/ticks;
        p->setPen(grid);
        p->drawLine(QPointF(x,WaveTop),QPointF(x,scoreBottom));
        p->setPen(muted);
        const QRectF rect(i==0?x:i==ticks?x-66:x-33,scoreBottom+1,66,21);
        p->drawText(rect,(i==0?Qt::AlignLeft:i==ticks?Qt::AlignRight:Qt::AlignHCenter)|Qt::AlignVCenter,
                    timeTick(qRound64(offset+span*i/ticks)));
    }
    p->setPen(axis);
    p->drawLine(QPointF(PlotLeft,waveMid),QPointF(width()-PlotRight,waveMid));
    p->drawLine(QPointF(PlotLeft,scoreBottom),QPointF(width()-PlotRight,scoreBottom));

    qreal peak=.05;
    QVector<QPointF> envelope;
    envelope.reserve(samples_.size());
    for (const auto &sample : samples_) {
        const auto bin=sample.toList();
        const QPointF value(bin.value(0).toDouble(),bin.value(1).toDouble());
        peak=std::max({peak,std::abs(value.x()),std::abs(value.y())});
        envelope.append(value);
    }
    const qreal scale=(WaveHeight/2-3)/peak;
    QPainterPath trace;
    for (int i=0;i<envelope.size();++i) {
        const qreal x=PlotLeft+plotWidth*(i+.5)/envelope.size();
        trace.moveTo(x,waveMid-envelope[i].x()*scale);
        trace.lineTo(x,waveMid-envelope[i].y()*scale);
    }
    p->setPen(QColor("#4c7fa5"));
    p->drawPath(trace);
    p->setPen(ink);
    p->drawText(QPointF(8,WaveTop+10),tr("波形 · FS"));
    p->drawText(QPointF(8,scoreTop+10),tr("余弦分数"));
    p->setPen(muted);
    p->drawText(QPointF(8,waveMid+11),QStringLiteral("±%1").arg(peak,0,'f',2));
    for (int i=0;i<lanes_.size();++i) {
        const qreal y=LaneTop+i*LaneHeight;
        p->fillRect(QRectF(PlotLeft,y,plotWidth,LaneHeight-2),QColor(i%2?"#f6f8fa":"#edf2f6"));
        p->setPen(ink);
        p->drawText(QRectF(8,y,PlotLeft-14,LaneHeight),Qt::AlignVCenter|Qt::AlignLeft,
            p->fontMetrics().elidedText(lanes_[i].label,Qt::ElideRight,int(PlotLeft-14)));
    }

    if (selectedIndex_>=0 && selectedIndex_<segments_.size()) {
        const auto selected=segments_[selectedIndex_].toMap();
        const auto x=timeX(selected.value("start_ms").toDouble());
        p->fillRect(QRectF(x,WaveTop,timeX(selected.value("end_ms").toDouble())-x,scoreBottom-WaveTop),
                    QColor(41,102,155,25));
    }
    const qreal threshold=metrics_.value("threshold",.77).toDouble();
    const qreal thresholdY=scoreBottom-threshold*ScoreHeight;
    p->setPen(QPen(QColor("#9b6836"),1,Qt::DashLine));
    p->drawLine(QPointF(PlotLeft,thresholdY),QPointF(width()-PlotRight,thresholdY));
    p->setPen(QColor("#875c32"));
    p->drawText(QPointF(8,thresholdY+18),tr("阈值 %1").arg(threshold,0,'f',2));
    for (int i=0;i<segments_.size();++i) {
        const auto row=segments_[i].toMap();
        const auto it=std::find_if(lanes_.cbegin(),lanes_.cend(),[&](const Lane &lane){return lane.key==rowKey(row);});
        if (it==lanes_.cend())
            continue;
        const qreal x=timeX(row.value("start_ms").toDouble());
        const qreal w=std::max(2.,timeX(row.value("end_ms").toDouble())-x);
        const qreal y=LaneTop+(it-lanes_.cbegin())*LaneHeight+4;
        const QRectF block(x,y,w,LaneHeight-10);
        p->fillRect(block,it->color);
        if (i==selectedIndex_) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(ink,2));
            p->drawRect(block);
        }
        if (row.value("similarity").isValid()) {
            const qreal dot=scoreBottom-ScoreHeight*std::clamp(row.value("similarity").toDouble(),0.,1.);
            p->setPen(grid);
            p->drawLine(QPointF(x+w/2,dot),QPointF(x+w/2,scoreBottom));
            p->setPen(Qt::NoPen);
            p->setBrush(it->color);
            p->drawEllipse(QPointF(x+w/2,dot),i==selectedIndex_?5:3.5,i==selectedIndex_?5:3.5);
        }
    }
}

MicrophoneArrayItem::MicrophoneArrayItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void MicrophoneArrayItem::setPositions(const QVariantList &value)
{
    if (positions_!=value) { positions_=value; update(); emit dataChanged(); }
}

void MicrophoneArrayItem::setLevels(const QVariantList &value)
{
    if (levels_!=value) { levels_=value; update(); emit dataChanged(); }
}

void MicrophoneArrayItem::setObservation(const QVariantMap &value)
{
    if (observation_!=value) { observation_=value; update(); emit dataChanged(); }
}

void MicrophoneArrayItem::paint(QPainter *p)
{
    p->fillRect(boundingRect(),Qt::white);
    const qreal r=std::min(width(),height())/2-23;
    if (r<12)
        return;
    const QPointF center(width()/2,height()/2);
    p->setFont(plotFont());
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(grid,1,Qt::DashLine));
    p->drawLine(center-QPointF(r+12,0),center+QPointF(r+12,0));
    p->drawLine(center-QPointF(0,r+12),center+QPointF(0,r+12));
    p->setPen(axis);
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(center,r,r);
    if (observation_.value("state")=="localized") {
        const QPointF tip=center+QPointF(observation_.value("array_x").toDouble(),-observation_.value("array_y").toDouble())*(r+9);
        const auto angle=std::atan2((tip-center).y(),(tip-center).x());
        p->setPen(QPen(QColor("#c17c31"),2));
        p->drawLine(center,tip);
        if (QLineF(center,tip).length()>10) {
            p->drawLine(tip,tip-QPointF(std::cos(angle-.55),std::sin(angle-.55))*8);
            p->drawLine(tip,tip-QPointF(std::cos(angle+.55),std::sin(angle+.55))*8);
        }
    }
    for (const auto &value : positions_) {
        const auto mic=value.toMap();
        const int channel=mic.value("channel").toInt();
        const QPointF v(mic.value("x").toDouble(),-mic.value("y").toDouble());
        const auto db=levels_.value(channel-1,-100).toDouble();
        const QPointF dot=center+v*r;
        p->setBrush(db>-55?QColor("#5d92b1"):QColor("#d2dce3"));
        p->setPen(QPen(QColor("#46647b"),1));
        p->drawEllipse(dot,5,5);
        p->setPen(ink);
        p->drawText(QRectF(dot+v*13-QPointF(13,8),QSizeF(26,16)),Qt::AlignCenter,QString::number(channel));
    }
}
