#include "AcousticPlots.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr qreal PlotLeft = 111, PlotRight = 72, WaveTop = 22, WaveHeight = 66;
constexpr qreal SpectrumTop = 130, SpectrumHeight = 115, OverlapTop = 269, LaneTop = 290, LaneHeight = 21;
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

double timeStep(double span, int count)
{
    const double target = span/count;
    const double magnitude = std::pow(10.,std::floor(std::log10(target)));
    for (const double multiplier : {1.,2.,2.5,5.,10.})
        if (multiplier*magnitude >= target)
            return multiplier*magnitude;
    return magnitude*10.;
}

void plotBox(QPainter *p, const QRectF &area, const QList<qreal> &ticks, const QColor &gridColor)
{
    p->setPen(QPen(gridColor,1));
    for (const auto x : ticks)
        p->drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
    p->setPen(QPen(QColor("#343a40"),1));
    p->setBrush(Qt::NoBrush);
    p->drawRect(area);
    for (const auto x : ticks) {
        p->drawLine(QPointF(x,area.bottom()),QPointF(x,area.bottom()+4));
        p->drawLine(QPointF(x,area.top()),QPointF(x,area.top()+3));
    }
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
    return int(std::ceil(LaneTop+std::max<qsizetype>(1,lanes_.size())*LaneHeight+44+extraPlotHeight()));
}

qreal SpeakerTimelineItem::extraPlotHeight() const
{
    const auto minimum = LaneTop+std::max<qsizetype>(1,lanes_.size())*LaneHeight+44;
    return qMax(0.,viewportHeight_-minimum);
}

void SpeakerTimelineItem::setViewportHeight(qreal value)
{
    if (viewportHeight_ != value) {
        viewportHeight_ = value;
        refresh();
    }
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

void SpeakerTimelineItem::setProfileId(const QString &value)
{
    if (profileId_ != value) {
        profileId_ = value;
        refresh();
    }
}

void SpeakerTimelineItem::setViewStart(double value)
{
    if (viewStart_ != value) {
        viewStart_ = value;
        refresh();
    }
}

void SpeakerTimelineItem::setViewSpan(double value)
{
    if (viewSpan_ != value) {
        viewSpan_ = value;
        refresh();
    }
}

void SpeakerTimelineItem::refresh()
{
    update();
    emit dataChanged();
}

void SpeakerTimelineItem::rebuildLanes()
{
    static const std::array<QColor,12> colors{{matlabBlue,QColor("#d95319"),QColor("#7e2f8e"),
        QColor("#548514"),QColor("#247f9b"),QColor("#a2142f"),QColor("#8f690d"),QColor("#066958"),
        QColor("#3e4f9c"),QColor("#a03970"),QColor("#795143"),QColor("#575b66")}};
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
            const bool meeting = key.startsWith(QStringLiteral("unconfirmed-"));
            const auto color = meeting ? colors[(profiles_.size()+lanes_.size())%colors.size()].darker(115) : QColor("#71849a");
            lanes_.append({key,label.isEmpty()?tr("未确认"):label,color});
        }
    }
    const auto rank=[&](const Lane &lane) {
        return std::find_if(profiles_.cbegin(),profiles_.cend(),[&](const QVariant &profile) {
            return profile.toMap().value("id").toString()==lane.key;
        })-profiles_.cbegin();
    };
    std::stable_sort(lanes_.begin(),lanes_.end(),[&](const Lane &a,const Lane &b){return rank(a)<rank(b);});
    ++axesRevision_;
    emit lanesChanged();
}

int SpeakerTimelineItem::segmentAt(qreal x, qreal y) const
{
    if (x<PlotLeft || x>width()-PlotRight)
        return -1;
    const auto duration = std::max(1000.,metrics_.value("audio_ms").toDouble());
    const auto t=metrics_.value("offset_ms").toDouble()+duration*(viewStart_ +
        viewSpan_*(x-PlotLeft)/std::max(qreal(1),width()-PlotLeft-PlotRight));
    const qreal laneTop = LaneTop+extraPlotHeight();
    const int lane = y >= laneTop ? int((y-laneTop)/LaneHeight) : -1;
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

QColor SpeakerTimelineItem::segmentColor(int index) const
{
    if (index >= 0 && index < segments_.size()) {
        const auto key = rowKey(segments_[index].toMap());
        for (const auto &lane : lanes_)
            if (lane.key == key)
                return lane.color;
    }
    return QColor("#71849a");
}

int SpeakerTimelineItem::segmentPosition(int index) const
{
    if (index >= 0 && index < segments_.size()) {
        const auto key = rowKey(segments_[index].toMap());
        for (int i=0; i<lanes_.size(); ++i)
            if (lanes_[i].key == key)
                return qRound(LaneTop+extraPlotHeight()+i*LaneHeight);
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
    const auto extra = extraPlotHeight();
    const auto waveHeight = WaveHeight + extra*.35;
    const auto spectrumTop = SpectrumTop + extra*.35;
    const auto spectrumHeight = SpectrumHeight + extra*.65;
    const auto overlapTop = OverlapTop + extra;
    const auto laneTop = LaneTop + extra;
    const qreal base=metrics_.value("offset_ms").toDouble();
    const qreal audioMs=metrics_.value("audio_ms").toDouble();
    const qreal duration=std::max(1000.,audioMs);
    const qreal offset=base+duration*viewStart_;
    const qreal span=std::max(1.,duration*viewSpan_);
    const qreal waveMid=WaveTop+waveHeight/2;
    const qreal right=PlotLeft+plotWidth;
    const qreal bottom=chartHeight()-44;
    const QRectF spectrum(PlotLeft,spectrumTop,plotWidth,spectrumHeight);
    const auto timeX=[&](double t){return PlotLeft+(t-offset)*plotWidth/span;};
    const QRectF wave(PlotLeft,WaveTop,plotWidth,waveHeight);
    const QRectF activity(PlotLeft,overlapTop,plotWidth,bottom-overlapTop);
    const auto panelTitle=[&](const QRectF &area,const QString &value,bool timeLabels=false) {
        auto font=plotFont();
        font.setBold(true);
        p->setFont(font);
        p->setPen(ink);
        p->drawText(QRectF(area.left(),area.top()-22,area.width(),17),Qt::AlignLeft|Qt::AlignVCenter,value);
        p->setFont(plotFont());
        if (timeLabels) {
            p->setPen(muted);
            p->drawText(QRectF(area.left(),area.top()-22,area.width(),17),Qt::AlignRight|Qt::AlignVCenter,
                        tr("时间 (分:秒)"));
        }
    };
    const auto yTick=[&](qreal y,const QString &value) {
        p->setPen(QColor("#343a40"));
        p->drawLine(QPointF(PlotLeft-4,y),QPointF(PlotLeft,y));
        p->drawText(QRectF(21,y-8,PlotLeft-30,16),Qt::AlignRight|Qt::AlignVCenter,value);
    };
    const auto yLabel=[&](const QRectF &area,const QString &value) {
        p->save();
        p->translate(20,area.center().y());
        p->rotate(-90);
        p->setPen(ink);
        p->drawText(QRectF(-area.height()/2,-9,area.height(),18),Qt::AlignCenter,value);
        p->restore();
    };
    p->fillRect(spectrum,spectrogram_.isNull() ? QColor("#f8f9fa") : QColor("#25205d"));
    if (!spectrogram_.isNull()) {
        const qreal representedMs=spectrogram_.width()*metrics_.value("spectrum_step_ms",40).toDouble();
        const qreal start=offset-base;
        const qreal end=std::min({representedMs,audioMs,start+span});
        if (end>start) {
            const qreal columnsPerMs=spectrogram_.width()/representedMs;
            p->setRenderHint(QPainter::SmoothPixmapTransform,true);
            p->drawImage(QRectF(PlotLeft,spectrumTop,plotWidth*(end-start)/span,spectrumHeight),
                         spectrogram_,QRectF(start*columnsPerMs,0,(end-start)*columnsPerMs,spectrogram_.height()));
            p->setRenderHint(QPainter::SmoothPixmapTransform,false);
        }
    }
    p->fillRect(QRectF(PlotLeft,overlapTop,plotWidth,17),QColor("#fff5df"));
    for (int i=0; i<std::max<qsizetype>(1,lanes_.size()); ++i) {
        const qreal y=laneTop+i*LaneHeight;
        if (i%2==0)
            p->fillRect(QRectF(PlotLeft,y,plotWidth,LaneHeight),QColor("#f5f7fa"));
        p->setPen(grid);
        p->drawLine(QPointF(PlotLeft,y),QPointF(right,y));
    }
    QList<qreal> ticks;
    const double step=timeStep(span,std::clamp(int(plotWidth/75),2,12));
    for (double time=std::ceil(offset/step)*step; time<=offset+span; time+=step) {
        const qreal x=timeX(time);
        ticks.append(x);
        p->setPen(ink);
        p->drawText(QRectF(x-31,wave.bottom()+4,62,15),Qt::AlignCenter,timeTick(qRound64(time),step<1000.));
        p->drawText(QRectF(x-31,bottom+5,62,17),Qt::AlignCenter,timeTick(qRound64(time),step<1000.));
    }
    for (const auto &area : {wave,spectrum,activity})
        plotBox(p,area,ticks,area==spectrum && !spectrogram_.isNull() ? QColor(255,255,255,48) : grid);
    panelTitle(wave,tr("时域波形"),true);
    panelTitle(spectrum,tr("时频声谱"));
    panelTitle(activity,tr("发言活动"));
    yLabel(wave,tr("幅值 (FS)"));
    yLabel(spectrum,tr("频率 (kHz)"));
    p->setPen(ink);
    p->drawText(QRectF(PlotLeft,bottom+24,plotWidth,16),Qt::AlignCenter,tr("时间 (分:秒)"));
    p->drawText(QRectF(28,overlapTop,PlotLeft-36,17),Qt::AlignRight|Qt::AlignVCenter,tr("多人同讲"));
    if (lanes_.isEmpty())
        p->drawText(QRectF(28,laneTop,PlotLeft-36,LaneHeight),Qt::AlignRight|Qt::AlignVCenter,tr("发言人"));
    for (int i=0; i<=4; ++i) {
        const qreal y=spectrumTop+spectrumHeight*i/4;
        if (i>0 && i<4) {
            p->setPen(spectrogram_.isNull() ? grid : QColor(255,255,255,62));
            p->drawLine(QPointF(PlotLeft,y),QPointF(right,y));
        }
        yTick(y,QString::number(8-i*2));
    }
    if (!spectrogram_.isNull()) {
        const auto palette=spectrogram_.colorTable();
        if (!palette.isEmpty()) {
            QLinearGradient gradient(0,spectrumTop,0,spectrum.bottom());
            for (int i=0; i<=8; ++i)
                gradient.setColorAt(1.-i/8.,QColor::fromRgb(palette[i*(palette.size()-1)/8]));
            const QRectF bar(right+12,spectrumTop,10,spectrumHeight);
            p->fillRect(bar,gradient);
            p->setPen(QColor("#343a40"));
            p->drawRect(bar);
            p->drawText(QRectF(bar.left()-3,bar.top()-19,45,16),Qt::AlignLeft,tr("dBFS"));
            for (const int level : {-20,-40,-60,-80}) {
                const qreal y=bar.top()+(-20-level)*bar.height()/65.;
                p->drawLine(QPointF(bar.right(),y),QPointF(bar.right()+4,y));
                p->drawText(QRectF(bar.right()+7,y-8,37,16),Qt::AlignVCenter,QString::number(level));
            }
        }
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
    peak = timeStep(peak,1);
    for (const double fraction : {1.,.5,0.,-.5,-1.}) {
        const qreal y=waveMid-fraction*waveHeight/2;
        if (std::abs(fraction)<1.) {
            p->setPen(fraction==0. ? axis : grid);
            p->drawLine(QPointF(PlotLeft,y),QPointF(right,y));
        }
        yTick(y,QString::number(peak*fraction,'g',3));
    }
    const qreal scale=waveHeight/(2*peak);
    QPainterPath trace;
    for (int i=0; i<envelope.size(); ++i) {
        const qreal x=timeX(base+audioMs*(i+.5)/envelope.size());
        if (x<PlotLeft || x>right)
            continue;
        trace.moveTo(x,waveMid-envelope[i].x()*scale);
        trace.lineTo(x,waveMid-envelope[i].y()*scale);
    }
    p->setPen(matlabBlue);
    p->drawPath(trace);

    for (const auto &value : metrics_.value("overlaps").toList()) {
        const auto overlap=value.toMap();
        const qreal x=std::max(PlotLeft,timeX(overlap.value("start_ms").toDouble()));
        const qreal w=std::min(right,timeX(overlap.value("end_ms").toDouble()))-x;
        if (w<=0)
            continue;
        p->fillRect(QRectF(x,overlapTop,w,16),QColor("#e6a432"));
        p->fillRect(QRectF(x,spectrumTop,w,3),QColor("#ffd264"));
        if (w>=28) {
            p->setPen(ink);
            p->drawText(QRectF(x,overlapTop,w,16),Qt::AlignCenter,overlap.value("speakers").toString());
        }
    }
    for (int i=0; i<lanes_.size(); ++i) {
        const qreal y=laneTop+i*LaneHeight;
        p->fillRect(QRectF(18,y+7,7,8),lanes_[i].color);
        p->setPen(QColor("#343a40"));
        p->drawLine(QPointF(PlotLeft-4,y+LaneHeight/2),QPointF(PlotLeft,y+LaneHeight/2));
        p->setPen(ink);
        p->drawText(QRectF(28,y,PlotLeft-36,LaneHeight),Qt::AlignRight|Qt::AlignVCenter,
            p->fontMetrics().elidedText(lanes_[i].label,Qt::ElideRight,PlotLeft-36));
    }
    for (int i=0; i<segments_.size(); ++i) {
        const auto row=segments_[i].toMap();
        const auto it=std::find_if(lanes_.cbegin(),lanes_.cend(),[&](const Lane &lane){return lane.key==rowKey(row);});
        if (it==lanes_.cend())
            continue;
        const qreal x=std::max(PlotLeft,timeX(row.value("start_ms").toDouble()));
        const qreal w=std::min(right,timeX(row.value("end_ms").toDouble()))-x;
        if (w<=0)
            continue;
        const qreal y=laneTop+(it-lanes_.cbegin())*LaneHeight+4;
        const QRectF block(x,y,w,LaneHeight-8);
        auto color=it->color;
        if (!profileId_.isEmpty() && profileId_ != it->key)
            color.setAlpha(68);
        p->fillRect(block,color);
        const auto phrases = row.value("transcript_spans").toList();
        if (phrases.isEmpty() && w>=62 && !row.value("transcript").toString().isEmpty()) {
            p->setPen(Qt::white);
            p->drawText(block.adjusted(4,0,-4,0),Qt::AlignVCenter,
                p->fontMetrics().elidedText(row.value("transcript").toString(),Qt::ElideRight,int(w-8)));
        }
        for (const auto &value : phrases) {
            const auto phrase = value.toMap();
            const qreal start = std::max(x,timeX(phrase.value("start_ms").toDouble()));
            const qreal length = std::min(x+w,timeX(phrase.value("end_ms").toDouble()))-start;
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
