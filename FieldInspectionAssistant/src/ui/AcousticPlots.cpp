#include "AcousticPlots.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr qreal PlotLeft = 96, PlotRight = 55, WaveTop = 16, WaveHeight = 42;
constexpr qreal SpectrumTop = 70, SpectrumHeight = 119, OverlapTop = 202, LaneTop = 225, LaneHeight = 23;
const QColor ink("#263e53"), muted("#586c7c"), grid("#e0e7ed"), axis("#aab9c6");
const QColor matlabBlue("#0072bd"), matlabOrange("#d95319");

QString rowKey(const QVariantMap &row)
{
    return row.value("profile_id",row.value("track_id",row.value("state"))).toString();
}

QString timeTick(qint64 milliseconds, bool fractional)
{
    const auto units = qRound64(milliseconds / (fractional ? 100. : 1000.));
    const auto seconds = fractional ? units / 10 : units;
    auto label = QStringLiteral("%1:%2").arg(seconds/60,2,10,QLatin1Char('0')).arg(seconds%60,2,10,QLatin1Char('0'));
    if (fractional)
        label += QStringLiteral(".%1").arg(units % 10);
    return label;
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
    return int(LaneTop+std::max<qsizetype>(1,lanes_.size())*LaneHeight+30);
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

void SpeakerTimelineItem::setSpectrogram(const QImage &value)
{
    if (spectrogram_.cacheKey() != value.cacheKey()) {
        spectrogram_ = value;
        refresh();
    }
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
    static const std::array<QColor,7> colors{{matlabBlue,QColor("#d95319"),QColor("#7e2f8e"),
        QColor("#77ac30"),QColor("#4dbeee"),QColor("#a2142f"),QColor("#b68918")}};
    lanes_.clear();
    for (const auto &value : segments_) {
        const auto row=value.toMap();
        const auto key=rowKey(row);
        if (std::any_of(lanes_.cbegin(),lanes_.cend(),[&](const Lane &lane){return lane.key==key;}))
            continue;
        if (!row.value("profile_id").toString().isEmpty()) {
            const auto it=std::find_if(profiles_.cbegin(),profiles_.cend(),[&](const QVariant &profile) {
                return profile.toMap().value("id")==key;
            });
            lanes_.append({key,row.value("speaker").toString(),colors[(it-profiles_.cbegin())%colors.size()]});
        } else {
            const auto label = row.value("speaker").toString();
            lanes_.append({key,label.isEmpty()?tr("未确认"):label,QColor("#71849a")});
        }
    }
}

int SpeakerTimelineItem::segmentAt(qreal x, qreal y) const
{
    if (x<PlotLeft || x>width()-PlotRight)
        return -1;
    const auto t=metrics_.value("offset_ms").toDouble()+
        (x-PlotLeft)*std::max(1000.,metrics_.value("audio_ms").toDouble())/
        std::max(qreal(1),width()-PlotLeft-PlotRight);
    const int lane = y >= LaneTop ? int((y-LaneTop)/LaneHeight) : -1;
    if (lane >= lanes_.size())
        return -1;
    for (int i=0;i<segments_.size();++i) {
        const auto segment=segments_[i].toMap();
        if (lane >= 0 && lanes_[lane].key != rowKey(segment))
            continue;
        if (t>=segment.value("start_ms").toDouble() && t<segment.value("end_ms").toDouble())
            return i;
    }
    return -1;
}

void SpeakerTimelineItem::paint(QPainter *p)
{
    p->fillRect(boundingRect(),Qt::white);
    const qreal plotWidth=width()-PlotLeft-PlotRight;
    if (plotWidth<20)
        return;
    p->setFont(plotFont());
    const qreal offset=metrics_.value("offset_ms").toDouble();
    const qreal audioMs=metrics_.value("audio_ms").toDouble();
    const qreal span=std::max(1000.,audioMs);
    const qreal waveMid=WaveTop+WaveHeight/2;
    const qreal right=PlotLeft+plotWidth;
    const qreal bottom=chartHeight()-29;
    const QRectF spectrum(PlotLeft,SpectrumTop,plotWidth,SpectrumHeight);
    const auto timeX=[&](double t){return PlotLeft+std::clamp((t-offset)/span,0.,1.)*plotWidth;};
    const auto label=[&](qreal y,const QString &value) {
        p->setPen(ink);
        p->drawText(QRectF(10,y,PlotLeft-16,17),Qt::AlignVCenter,p->fontMetrics().elidedText(value,Qt::ElideRight,PlotLeft-16));
    };

    p->fillRect(QRectF(PlotLeft,WaveTop,plotWidth,WaveHeight),QColor("#f8fafc"));
    p->fillRect(spectrum,QColor("#25205d"));
    if (!spectrogram_.isNull()) {
        const qreal representedMs=spectrogram_.width()*metrics_.value("spectrum_step_ms",40).toDouble();
        const qreal visibleMs=std::min(representedMs,audioMs);
        const qreal columns=spectrogram_.width()*visibleMs/std::max(1.,representedMs);
        p->setRenderHint(QPainter::SmoothPixmapTransform,true);
        p->drawImage(QRectF(PlotLeft,SpectrumTop,plotWidth*visibleMs/span,SpectrumHeight),
                     spectrogram_,QRectF(0,0,columns,spectrogram_.height()));
        p->setRenderHint(QPainter::SmoothPixmapTransform,false);
    }
    p->fillRect(QRectF(PlotLeft,OverlapTop,plotWidth,16),QColor("#fff5df"));
    for (int i=0; i<std::max<qsizetype>(1,lanes_.size()); ++i)
        p->fillRect(QRectF(PlotLeft,LaneTop+i*LaneHeight,plotWidth,LaneHeight-1),QColor(i%2?"#edf2f6":"#f5f7fa"));

    for (int i=0,ticks=std::clamp(int(plotWidth/84),2,10); i<=ticks; ++i) {
        const qreal x=PlotLeft+plotWidth*i/ticks;
        p->setPen(grid);
        p->drawLine(QPointF(x,WaveTop),QPointF(x,WaveTop+WaveHeight));
        p->drawLine(QPointF(x,OverlapTop),QPointF(x,bottom));
        p->setPen(QColor(255,255,255,48));
        p->drawLine(QPointF(x,SpectrumTop),QPointF(x,spectrum.bottom()));
        p->setPen(muted);
        p->drawText(QRectF(i==0?x:i==ticks?x-64:x-32,bottom+3,64,20),
                    (i==0?Qt::AlignLeft:i==ticks?Qt::AlignRight:Qt::AlignHCenter)|Qt::AlignVCenter,
                    timeTick(qRound64(offset+span*i/ticks),span<10000));
    }
    label(2,tr("波形 · FS"));
    label(OverlapTop-1,tr("多人同讲"));
    if (lanes_.isEmpty())
        label(LaneTop,tr("发言轨道"));
    p->setPen(muted);
    p->drawText(QPointF(10,SpectrumTop+5),tr("8 kHz"));
    p->drawText(QPointF(10,spectrum.bottom()-1),tr("0"));
    for (int i=1; i<4; ++i) {
        const qreal y=SpectrumTop+SpectrumHeight*i/4;
        p->setPen(QColor(255,255,255,62));
        p->drawLine(QPointF(PlotLeft,y),QPointF(right,y));
        p->setPen(muted);
        p->drawText(QPointF(10,y+4),QString::number(8-i*2));
    }
    if (!spectrogram_.isNull()) {
        const auto palette=spectrogram_.colorTable();
        if (!palette.isEmpty()) {
            QLinearGradient gradient(0,SpectrumTop,0,spectrum.bottom());
            for (int i=0; i<=8; ++i)
                gradient.setColorAt(1.-i/8.,QColor::fromRgb(palette[i*(palette.size()-1)/8]));
            p->fillRect(QRectF(right+7,SpectrumTop,8,SpectrumHeight),gradient);
            p->setPen(muted);
            p->drawText(QPointF(right+20,SpectrumTop+8),QStringLiteral("−20"));
            p->drawText(QPointF(right+20,SpectrumTop+SpectrumHeight/2),QStringLiteral("dBFS"));
            p->drawText(QPointF(right+20,spectrum.bottom()),QStringLiteral("−85"));
        }
    } else {
        p->setPen(QColor("#c6cee6"));
        p->drawText(spectrum,Qt::AlignCenter,tr("声谱图"));
    }

    qreal peak=.05;
    QVector<QPointF> envelope;
    envelope.reserve(samples_.size());
    for (const auto &sample : samples_) {
        const auto bin=sample.toList();
        const QPointF value(bin.value(0).toDouble(),bin.value(1).toDouble());
        peak=std::max({peak,std::abs(value.x()),std::abs(value.y())});
        envelope.append(value);
    }
    const qreal scale=(WaveHeight/2-2)/peak;
    QPainterPath trace;
    for (int i=0; i<envelope.size(); ++i) {
        const qreal x=timeX(offset+audioMs*(i+.5)/envelope.size());
        trace.moveTo(x,waveMid-envelope[i].x()*scale);
        trace.lineTo(x,waveMid-envelope[i].y()*scale);
    }
    p->setPen(axis);
    p->drawLine(QPointF(PlotLeft,waveMid),QPointF(right,waveMid));
    p->drawLine(QPointF(PlotLeft,bottom),QPointF(right,bottom));
    p->setPen(muted);
    p->drawText(QPointF(10,waveMid+5),QStringLiteral("±%1").arg(peak,0,'f',2));
    p->setPen(matlabBlue);
    p->drawPath(trace);

    for (const auto &value : metrics_.value("overlaps").toList()) {
        const auto overlap=value.toMap();
        const qreal x=timeX(overlap.value("start_ms").toDouble());
        const qreal w=timeX(overlap.value("end_ms").toDouble())-x;
        p->fillRect(QRectF(x,OverlapTop,w,16),QColor("#e6a432"));
        p->fillRect(QRectF(x,SpectrumTop,w,3),QColor("#ffd264"));
    }
    for (int i=0; i<lanes_.size(); ++i) {
        const qreal y=LaneTop+i*LaneHeight;
        p->fillRect(QRectF(11,y+7,7,8),lanes_[i].color);
        p->setPen(ink);
        p->drawText(QRectF(23,y,PlotLeft-26,LaneHeight),Qt::AlignVCenter,
            p->fontMetrics().elidedText(lanes_[i].label,Qt::ElideRight,PlotLeft-26));
    }
    for (int i=0; i<segments_.size(); ++i) {
        const auto row=segments_[i].toMap();
        const auto it=std::find_if(lanes_.cbegin(),lanes_.cend(),[&](const Lane &lane){return lane.key==rowKey(row);});
        if (it==lanes_.cend())
            continue;
        const qreal x=timeX(row.value("start_ms").toDouble());
        const qreal w=std::max(1.,timeX(row.value("end_ms").toDouble())-x);
        const qreal y=LaneTop+(it-lanes_.cbegin())*LaneHeight+4;
        const QRectF block(x,y,w,LaneHeight-8);
        p->fillRect(block,it->color);
        const auto phrases = row.value("transcript_spans").toList();
        if (phrases.isEmpty() && w>=62 && !row.value("transcript").toString().isEmpty()) {
            p->setPen(Qt::white);
            p->drawText(block.adjusted(4,0,-4,0),Qt::AlignVCenter,
                p->fontMetrics().elidedText(row.value("transcript").toString(),Qt::ElideRight,int(w-8)));
        }
        for (const auto &value : phrases) {
            const auto phrase = value.toMap();
            const qreal start = timeX(phrase.value("start_ms").toDouble());
            const qreal length = timeX(phrase.value("end_ms").toDouble())-start;
            if (length < 45)
                continue;
            p->setPen(Qt::white);
            p->drawText(QRectF(start+4,y,length-8,LaneHeight-8),Qt::AlignVCenter,
                        p->fontMetrics().elidedText(phrase.value("text").toString(),Qt::ElideRight,int(length-8)));
        }
        if (i==selectedIndex_) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(ink,2));
            p->drawRect(block);
            p->setPen(QPen(QColor("#7289a1"),1,Qt::DashLine));
            p->drawLine(QPointF(x,WaveTop),QPointF(x,bottom));
            p->drawLine(QPointF(x+w,WaveTop),QPointF(x+w,bottom));
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
