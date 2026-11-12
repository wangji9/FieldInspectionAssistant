#include <QGuiApplication>
#include "NativeHeatmapItem.h"
#include <QPainter>
#include <QDateTime>
#include <QLinearGradient>
#include <cmath>
#include <algorithm>
#include "../services/SignalAnalysisService.h"
namespace{struct RenderCost{QElapsedTimer timer;double &value;RenderCost(double &v):value(v){timer.start();}~RenderCost(){value=timer.nsecsElapsed()/1e6;}};}
NativeHeatmapItem::NativeHeatmapItem(QQuickItem *parent):QQuickPaintedItem(parent){setAntialiasing(false);setOpaquePainting(true);connect(this,&NativeHeatmapItem::rangeChanged,this,[this]{update();});connect(this,&QQuickItem::visibleChanged,this,&NativeHeatmapItem::refreshSource);}
void NativeHeatmapItem::setMatlabStyle(bool value){if(matlabStyle_==value)return;matlabStyle_=value;auto data=plot_;setPlotData(data);emit styleChanged();}
void NativeHeatmapItem::setDarkStyle(bool value)
{
    if (darkStyle_ == value)
        return;
    darkStyle_ = value;
    const auto data = plot_;
    setPlotData(data);
    emit styleChanged();
}
void NativeHeatmapItem::setAnalysisSource(QObject *source){if(source_==source)return;disconnect(sourceConnection_);source_=source;if(auto *service=qobject_cast<SignalAnalysisService*>(source))sourceConnection_=connect(service,&SignalAnalysisService::snapshotChanged,this,&NativeHeatmapItem::refreshSource);QMetaObject::invokeMethod(this,&NativeHeatmapItem::refreshSource,Qt::QueuedConnection);emit sourceChanged();}
void NativeHeatmapItem::setAnalysisField(const QString&field){if(field_==field)return;field_=field;QMetaObject::invokeMethod(this,&NativeHeatmapItem::refreshSource,Qt::QueuedConnection);emit sourceChanged();}
void NativeHeatmapItem::refreshSource(){if(!isVisible())return;if(auto *service=qobject_cast<SignalAnalysisService*>(source_.data())){QVariant value=service->snapshot();for(const auto &key:field_.split('.'))value=value.toMap().value(key);setPlotData(value.toMap());}}
void NativeHeatmapItem::setPlotData(const QVariantMap &plot){
    RenderCost cost(decodeMs_);
    plot_=plot;linePoints_.clear();lineColors_.clear();double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
    const QStringList palette=darkStyle_ ? (plot.value("method_id") == "fft"
        ? QStringList{"#71e3ff", "#48b4df", "#1f8dff", "#a3f5fa", "#a8b5ff", "#65dbb5"}
        : QStringList{"#8bf92f", "#57d826", "#c7f956", "#49b53e", "#7cec67", "#2c9738"})
        : matlabStyle_?QStringList{"#0072BD","#D95319","#EDB120","#7E2F8E","#77AC30","#4DBEEE","#A2142F"}:QStringList{"#6D63FF","#0BAE91","#FF65AE","#EF9B41","#4895EF","#9154C6","#6B8B38","#DA6373","#525A89"};
    for(auto entry:plot.value("series").toList()){auto s=entry.toMap();QPolygonF polygon;auto points=s["points"].toList();polygon.reserve(points.size());for(auto v:points){auto p=v.toList();double x=p.value(0).toDouble(),y=p.value(1).toDouble();if(!std::isfinite(x)||!std::isfinite(y))continue;polygon.append(QPointF(x,y));xmin=qMin(xmin,x);xmax=qMax(xmax,x);ymin=qMin(ymin,y);ymax=qMax(ymax,y);}lineColors_.append(QColor(matlabStyle_?palette[linePoints_.size()%palette.size()]:s.value("color",palette[linePoints_.size()%palette.size()]).toString()));linePoints_.append(polygon);}
    dataBounds_=QRectF(QPointF(xmin,ymin),QPointF(xmax,ymax));auto rows=plot.value("matrix").toList();auto valid=plot.value("validFrames").toList();
    if(rows.isEmpty()){image_=QImage();update();emit plotDataChanged();return;}
    int height=rows.size(),width=rows.first().toList().size();if(!width||height>4096||width>4096){image_=QImage();plot_["matrix"]=QVariantList();plot_["error"]=QStringLiteral("图表数据尺寸无效，请重新计算");update();emit plotDataChanged();return;}
    double minimum=INFINITY,maximum=-INFINITY;
    if(plot.contains("colorMin")){minimum=plot["colorMin"].toDouble();maximum=plot["colorMax"].toDouble();}
    else for(auto row:rows){auto values=row.toList();for(int c=0;c<values.size();c++){if(!valid.isEmpty()&&!valid.value(c).toBool())continue;double value=values[c].toDouble();minimum=qMin(minimum,value);maximum=qMax(maximum,value);}}
    image_=QImage(width,height,QImage::Format_RGB32);QRgb colors[256];for(int i=0;i<256;i++){double t=i/255.;auto component=[](double v){return qRound(qBound(0.,v,1.)*255);};colors[i]=qRgb(component(1.5-std::abs(4*t-3)),component(1.5-std::abs(4*t-2)),component(1.5-std::abs(4*t-1)));}
    for(int r=0;r<height;r++){auto values=rows[r].toList();auto *pixels=reinterpret_cast<QRgb*>(image_.scanLine(height-r-1));for(int c=0;c<width;c++)pixels[c]=!valid.isEmpty()&&!valid.value(c).toBool()?qRgb(230,232,239):colors[qBound(0,qRound((values.value(c).toDouble()-minimum)/qMax(1e-15,maximum-minimum)*255),255)];}
    update();emit plotDataChanged();
}
namespace {
// Small, continuous antialiased segments avoid the generic stroker's high cost
// on dense live traces in Qt Quick's software backend.
void rasterSegment(QImage &image,QPointF a,QPointF b,const QColor &color,const QRectF &clip){
    double dx=b.x()-a.x(),dy=b.y()-a.y(),lo=0,hi=1;
    auto edge=[&](double p,double q){if(std::abs(p)<1e-12)return q>=0;double t=q/p;if(p<0){if(t>hi)return false;lo=qMax(lo,t);}else{if(t<lo)return false;hi=qMin(hi,t);}return true;};
    if(!edge(-dx,a.x()-clip.left())||!edge(dx,clip.right()-a.x())||!edge(-dy,a.y()-clip.top())||!edge(dy,clip.bottom()-a.y()))return;
    QPointF origin=a;a=origin+QPointF(dx,dy)*lo;b=origin+QPointF(dx,dy)*hi;
    const int cr=color.red(),cg=color.green(),cb=color.blue();
    auto pixel=[&](int x,int y,double coverage){
        if(x<0||y<0||x>=image.width()||y>=image.height()||x<clip.left()||x>clip.right()||y<clip.top()||y>clip.bottom())return;
        int alpha=qBound(0,qRound(coverage*255),255);if(!alpha)return;auto &dst=reinterpret_cast<QRgb*>(image.scanLine(y))[x];int inv=255-alpha;
        dst=qRgba((cr*alpha+qRed(dst)*inv+127)/255,(cg*alpha+qGreen(dst)*inv+127)/255,(cb*alpha+qBlue(dst)*inv+127)/255,alpha+(qAlpha(dst)*inv+127)/255);
    };
    bool steep=std::abs(b.y()-a.y())>std::abs(b.x()-a.x());if(steep){a=QPointF(a.y(),a.x());b=QPointF(b.y(),b.x());}if(a.x()>b.x())std::swap(a,b);
    auto dot=[&](int x,int y,double v){if(steep)pixel(y,x,v);else pixel(x,y,v);};
    dx=b.x()-a.x();dy=b.y()-a.y();if(dx<1e-9){dot(qRound(a.x()),qRound(a.y()),1);return;}double gradient=dy/dx;
    auto fraction=[](double v){return v-std::floor(v);};
    int first=qRound(a.x()),last=qRound(b.x());double y=a.y()+gradient*(first-a.x()),gap=1-fraction(a.x()+.5);
    dot(first,int(std::floor(y)),(1-fraction(y))*gap);dot(first,int(std::floor(y))+1,fraction(y)*gap);double inter=y+gradient;
    y=b.y()+gradient*(last-b.x());gap=fraction(b.x()+.5);dot(last,int(std::floor(y)),(1-fraction(y))*gap);dot(last,int(std::floor(y))+1,fraction(y)*gap);
    for(int x=first+1;x<last;++x){int row=int(std::floor(inter));double f=fraction(inter);dot(x,row,1-f);dot(x,row+1,f);inter+=gradient;}
}
QString number(double v){if(v==0)return "0";double a=std::abs(v);if(a<.0001||a>=100000)return QString::number(v,'g',2);int exponent=int(std::floor(std::log10(a)));double scale=std::pow(10.,exponent-1);double rounded=std::round(v/scale)*scale;return QString::number(rounded,'f',qMax(0,1-exponent));}
QColor heat(double t){auto c=[](double v){return qRound(qBound(0.,v,1.)*255);};return QColor(c(1.5-std::abs(4*t-3)),c(1.5-std::abs(4*t-2)),c(1.5-std::abs(4*t-1)));}
}
QString NativeHeatmapItem::coordinates(double x,double y)const{
    if(!graph_.contains(x,y))return {};double value=xmin_+(x-graph_.left())/graph_.width()*(xmax_-xmin_);if(plot_.contains("nyquist_hz")&&value>plot_["nyquist_hz"].toDouble())return QStringLiteral("超出有效采样带宽");QString xText=plot_.value("xKind")=="time"?QDateTime::fromMSecsSinceEpoch(qint64(value*1000)).toString("hh:mm:ss"):plot_.value("xLabel").toString().contains("时间")?(xmax_-xmin_<.999999?QString::number(qRound64(value*1000))+" ms":QString::number(qRound64(value))):number(value*plot_.value("xDisplayScale",1).toDouble());return QStringLiteral("x: %1   y: %2").arg(xText,number(ymax_-(y-graph_.top())/graph_.height()*(ymax_-ymin_)));
}
void NativeHeatmapItem::paint(QPainter *p){
    RenderCost cost(paintMs_);
    p->fillRect(boundingRect(), darkStyle_ ? Qt::black : Qt::white);
    const auto kind=plot_.value("kind","line").toString();auto series=plot_.value("series").toList(),slices=plot_.value("slices").toList();
    p->setRenderHint(QPainter::Antialiasing,true);QFont font=QGuiApplication::font();font.setPixelSize(matlabStyle_?12:11);p->setFont(font);
    if(series.isEmpty()&&slices.isEmpty()&&image_.isNull()&&!darkStyle_){
        const auto error=plot_.value("error").toString();if(!error.isEmpty()){font.setPixelSize(14);p->setFont(font);p->setPen(QColor("#8790A7"));p->drawText(boundingRect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,error);}return;
    }
    const bool matrix=kind=="heatmap";graph_=QRectF(84,10,qMax(20.,width()-84-(matrix?83:30)),qMax(20.,height()-(matlabStyle_?38:58)));
    if (darkStyle_) {
        font.setPixelSize(11);
        p->setFont(font);
        graph_ = QRectF(62, 24, qMax(20., width() - 82), qMax(16., height() - 57));
        p->setPen(Qt::white);
        p->drawText(QRectF(0, 3, width(), 17), Qt::AlignCenter, plotTitle());
    }
    double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
    if(matrix){auto x=plot_["xRange"].toList(),y=plot_["yRange"].toList();xmin=x.value(0).toDouble();xmax=x.value(1).toDouble();ymin=y.value(0).toDouble();ymax=y.value(1).toDouble();}
    else if(kind=="waterfall"){
        auto datasets=kind=="waterfall"?slices:series;
        for(auto entry:datasets)for(auto point:entry.toMap().value("points").toList()){auto xy=point.toList();double x=xy.value(0).toDouble(),y=xy.value(1).toDouble();xmin=qMin(xmin,x);xmax=qMax(xmax,x);ymin=qMin(ymin,y);ymax=qMax(ymax,y);}
    }else if (darkStyle_ && series.isEmpty()) {
        const auto x = plot_.value("xRange").toList(), y = plot_.value("yRange").toList();
        xmin = x.value(0).toDouble(); xmax = x.value(1).toDouble();
        ymin = y.value(0).toDouble(); ymax = y.value(1).toDouble();
    }else{xmin=dataBounds_.left();xmax=dataBounds_.right();ymin=dataBounds_.top();ymax=dataBounds_.bottom();}
    if(!std::isfinite(xmin)||!std::isfinite(ymin))return;if(xmax<=xmin)xmax=xmin+1;
    if(!matrix&&plot_.contains("xRange")){auto range=plot_["xRange"].toList();xmin=range.value(0).toDouble();xmax=qMax(xmin+1e-9,range.value(1).toDouble());}
    const auto categoryLabels = plot_.value("xTickLabels").toStringList();
    const bool categorical = kind == "bar" && !categoryLabels.isEmpty();
    const bool secondsAxis=plot_.value("xLabel").toString().contains("时间")&&plot_.value("xKind")!="time";
    if(secondsAxis){xmin=std::floor(xmin);xmax=qMax(xmin+1.,std::ceil(xmax));}
    double start=qBound(0.,rangeStart_,.99999),end=qBound(start+.00001,rangeEnd_,1.),span=xmax-xmin;xmax=xmin+span*end;xmin+=span*start;
    if(manualY_){ymin=yMinimum_;ymax=qMax(ymin+1e-9,yMaximum_);}else if(!matrix&&!(darkStyle_&&series.isEmpty())){double pad=qMax(1e-12,(ymax-ymin)*.1);if(ymax==ymin)pad=qMax(.001,std::abs(ymin)*.1);ymin-=pad;ymax+=pad;}
    if(ymax<=ymin)ymax=ymin+1;
    xmin_=xmin;xmax_=xmax;ymin_=ymin;ymax_=ymax;
    auto X=[&](double x){return graph_.left()+(x-xmin)/(xmax-xmin)*graph_.width();};
    auto Y=[&](double y){return graph_.bottom()-(y-ymin)/(ymax-ymin)*graph_.height();};
    const bool subsecond=secondsAxis&&xmax-xmin<.999999;
    const QColor label(darkStyle_?"#d5d5d5":matlabStyle_?"#262626":"#7E879D"),grid(darkStyle_?"#2d2d2d":matlabStyle_?"#DDDDDD":"#E9EDF5");
    auto text=[&](double x,double y,const QString&s,int alignment=Qt::AlignCenter){p->setPen(label);p->drawText(QRectF(x-60,y-9,120,18),alignment|Qt::AlignVCenter,s);};
    if(kind=="waterfall"){
        auto project=[&](double f,double t,double z){return QPointF(graph_.left()+graph_.width()*(.78*(f-xmin)/(xmax-xmin)+.2*t),graph_.bottom()-graph_.height()*(.55*(z-ymin)/(ymax-ymin)+.35*t));};
        for(int i=0;i<=4;i++){
            double f=xmin+(xmax-xmin)*i/4,t=i/4.,z=ymin+(ymax-ymin)*i/4;p->setPen(grid);p->drawLine(project(f,0,ymin),project(f,1,ymin));p->drawLine(project(xmin,t,ymin),project(xmax,t,ymin));
            auto a=project(f,0,ymin),b=project(xmax,t,ymin),c=project(xmin,0,z);text(a.x(),a.y()+17,number(f));
            double first=slices.first().toMap()["time"].toDouble(),last=slices.last().toMap()["time"].toDouble();auto time=qRound64(first+(last-first)*t);auto previous=qRound64(first+(last-first)*(i-1)/4.);if(i==0||time!=previous)text(b.x()+25,b.y(),QString::number(time));text(c.x()-32,c.y(),number(z));
        }
        if(matlabStyle_){p->setPen(QPen(label,.8));p->drawLine(project(xmin,0,ymin),project(xmax,0,ymin));p->drawLine(project(xmin,0,ymin),project(xmin,0,ymax));p->drawLine(project(xmax,0,ymin),project(xmax,1,ymin));p->setPen(grid);p->drawLine(project(xmin,0,ymax),project(xmax,0,ymax));p->drawLine(project(xmin,0,ymax),project(xmin,1,ymax));p->drawLine(project(xmax,0,ymax),project(xmax,1,ymax));p->drawLine(project(xmin,1,ymax),project(xmax,1,ymax));}
        p->save();p->setClipRect(graph_);
        for(int i=slices.size()-1;i>=0;i--){double t=double(i)/qMax(1,int(slices.size())-1);QPolygonF poly;for(auto v:slices[i].toMap()["points"].toList()){auto xy=v.toList();poly.append(project(xy[0].toDouble(),t,xy[1].toDouble()));}p->setPen(QPen(heat(1-t*.85),1.1));p->drawPolyline(poly);}p->restore();
        if(matlabStyle_){p->setPen(label);p->drawText(QRectF(0,graph_.bottom()+9,graph_.left()-14,18),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("频率 / Hz"));auto anchor=project(xmax,.5,ymin)+QPointF(38,-4);p->save();p->translate(anchor);p->rotate(-55);p->drawText(QRectF(-40,-9,80,18),Qt::AlignCenter,QStringLiteral("时间 / s"));p->restore();}
        else{text(graph_.left()+graph_.width()*.39,height()-8,QStringLiteral("频率 / Hz"));text(graph_.right()-30,graph_.top()+graph_.height()*.55,QStringLiteral("时间 / s"));}
    }else{
        const int divisions = darkStyle_ && graph_.height() < 58 ? 2 : 4;
        for(int i=0;i<=divisions;i++){
            const double fraction = double(i) / divisions;
            const double x=graph_.left()+graph_.width()*fraction,y=graph_.top()+graph_.height()*fraction;
            p->setPen(grid);
            if(!categorical)p->drawLine(QPointF(x,graph_.top()),QPointF(x,graph_.bottom()));
            p->drawLine(QPointF(graph_.left(),y),QPointF(graph_.right(),y));
            const double value=xmin+(xmax-xmin)*fraction;
            const double scaled=value*plot_.value("xDisplayScale",1).toDouble();
            if(!secondsAxis&&!categorical)
                text(x,graph_.bottom()+(darkStyle_?13:18),plot_.value("xKind")=="time"
                    ? QDateTime::fromMSecsSinceEpoch(qint64(value*1000)).toString("hh:mm:ss")
                    : darkStyle_ ? QString::number(scaled, 'g', 4) : number(scaled));
            p->setPen(label);
            double yValue=ymax-(ymax-ymin)*fraction;
            if (darkStyle_ && std::abs(yValue) < (ymax - ymin) * .0001)
                yValue = 0;
            const bool integerY=plot_.value("integerY").toBool()||plot_.value("yLabel").toString().contains("编号");
            const auto yText=integerY?QString::number(qRound64(yValue)):darkStyle_?QString::number(yValue,'g',2):number(yValue);
            p->drawText(QRectF(23,y-9,graph_.left()-(darkStyle_?27:31),18),Qt::AlignRight|Qt::AlignVCenter,yText);
        }
        if (darkStyle_) {
            p->setPen(label);
            p->drawLine(graph_.topLeft(), graph_.bottomLeft());
            p->drawLine(graph_.bottomLeft(), graph_.bottomRight());
        }
        if (categorical) {
            const auto tickWidth = qMin(120.0, graph_.width() / (xmax - xmin) - 4.0);
            const QFontMetricsF metrics(p->font());
            for (qsizetype i = 0; i < categoryLabels.size(); ++i)
                if (i >= xmin && i <= xmax && tickWidth > 0)
                    text(X(i), graph_.bottom() + 18, metrics.elidedText(categoryLabels[i], Qt::ElideRight, tickWidth));
        }
        if(secondsAxis){double step=subsecond?(xmax-xmin)/4:qMax(1.,std::ceil((xmax-xmin)/6.));double first=subsecond?xmin:std::ceil(xmin/step)*step;for(double tick=first;tick<=xmax+step*.001;tick+=step){text(X(tick),graph_.bottom()+18,QString::number(qRound64(tick*(subsecond?1000:1))));if(matlabStyle_){p->setPen(QPen(label,.8));p->drawLine(QPointF(X(tick),graph_.bottom()),QPointF(X(tick),graph_.bottom()-5));p->drawLine(QPointF(X(tick),graph_.top()),QPointF(X(tick),graph_.top()+5));}}}
        if(matlabStyle_){
            p->setPen(QPen(label,.8));p->setBrush(Qt::NoBrush);p->drawRect(graph_);
            for(int i=0;i<=4;i++){double y=graph_.top()+graph_.height()*i/4;p->drawLine(QPointF(graph_.left(),y),QPointF(graph_.left()+5,y));p->drawLine(QPointF(graph_.right(),y),QPointF(graph_.right()-5,y));if(!secondsAxis&&!categorical){double x=graph_.left()+graph_.width()*i/4;p->drawLine(QPointF(x,graph_.bottom()),QPointF(x,graph_.bottom()-5));p->drawLine(QPointF(x,graph_.top()),QPointF(x,graph_.top()+5));}}
        }
        p->save();p->setClipRect(graph_);
        if(matrix){p->setRenderHint(QPainter::SmoothPixmapTransform,false);auto xr=plot_["xRange"].toList();double left=X(xr.value(0).toDouble()),right=X(xr.value(1).toDouble());p->drawImage(QRectF(left,graph_.top(),qMax(.1,right-left),graph_.height()),image_);}
        else if (kind == "bar") {
            const auto half = qMax(0.0, plot_.value("barWidth", 0.72).toDouble() * 0.5);
            for (qsizetype i = 0; i < linePoints_.size(); ++i) {
                const auto color = lineColors_[i];
                p->setPen(QPen(color.darker(118), 0.7));
                p->setBrush(color);
                for (const auto &point : linePoints_[i])
                    if (point.y() > 0)
                        p->drawRect(QRectF(QPointF(X(point.x() - half), Y(point.y())),
                                           QPointF(X(point.x() + half), Y(0))));
            }
        }
        else{
            QTransform transform(graph_.width()/(xmax-xmin),0,0,-graph_.height()/(ymax-ymin),graph_.left()-xmin*graph_.width()/(xmax-xmin),graph_.bottom()+ymin*graph_.height()/(ymax-ymin));
            auto *service=qobject_cast<SignalAnalysisService*>(source_.data());bool realtime=service&&service->running()&&kind!="scatter";p->setRenderHint(QPainter::Antialiasing,!realtime);
            const bool fastLines=matlabStyle_&&kind=="line";
            if(fastLines){QSize size(qCeil(width()),qCeil(height()));if(curveLayer_.size()!=size)curveLayer_=QImage(size,QImage::Format_ARGB32_Premultiplied);curveLayer_.fill(Qt::transparent);}
            for(int i=0;i<linePoints_.size();i++){
                QPen pen(lineColors_[i],kind=="scatter"?3.5:matlabStyle_?1.15:realtime?1.:1.3,Qt::SolidLine,matlabStyle_?Qt::RoundCap:realtime?Qt::FlatCap:Qt::RoundCap);pen.setCosmetic(true);pen.setJoinStyle(Qt::RoundJoin);p->setPen(pen);
                const auto &points=linePoints_[i];QPolygonF poly;
                // Restrict work to the visible interval, retaining boundary neighbours.
                auto first=kind=="scatter"?points.cbegin():std::lower_bound(points.cbegin(),points.cend(),xmin,[](const QPointF&a,double b){return a.x()<b;});
                auto last=kind=="scatter"?points.cend():std::upper_bound(points.cbegin(),points.cend(),xmax,[](double a,const QPointF&b){return a<b.x();});
                if(first!=points.cbegin())--first;if(last!=points.cend())++last;
                poly.reserve(qMin(int(last-first),int(graph_.width()*2+4)));
                for(auto it=first;it!=last;){
                    auto begin=it,low=it,high=it;int column=int(std::floor(X(it->x())));++it;
                    if(kind!="scatter")while(it!=last&&int(std::floor(X(it->x())))==column){if(it->y()<low->y())low=it;if(it->y()>high->y())high=it;++it;}
                    auto a=low<high?low:high,b=low<high?high:low;
                    if(begin==first)poly.append(transform.map(*begin));poly.append(transform.map(*a));if(a!=b)poly.append(transform.map(*b));if(it==last&&it-1!=b)poly.append(transform.map(*(it-1)));
                }
                if(fastLines){for(int k=1;k<poly.size();++k)rasterSegment(curveLayer_,poly[k-1],poly[k],lineColors_[i],graph_);}
                else if(kind=="scatter")p->drawPoints(poly);else p->drawPolyline(poly);
            }
            if(fastLines)p->drawImage(QPointF(0,0),curveLayer_);
        }p->restore();
        const auto xLabel=subsecond?QStringLiteral("时间 / ms"):plot_["xLabel"].toString();
        if(matlabStyle_){
            const auto firstTick=subsecond?QString::number(qRound64(xmin*1000)):plot_.value("xKind")=="time"?QDateTime::fromMSecsSinceEpoch(qint64(xmin*1000)).toString("hh:mm:ss"):number(xmin*plot_.value("xDisplayScale",1).toDouble());
            double available=graph_.left()-QFontMetricsF(p->font()).horizontalAdvance(firstTick)/2-8;
            p->save();auto labelFont=p->font();double labelWidth=QFontMetricsF(labelFont).horizontalAdvance(xLabel);if(labelWidth>available)labelFont.setPixelSize(qMax(9,int(labelFont.pixelSize()*available/labelWidth)));p->setFont(labelFont);p->setPen(label);p->drawText(QRectF(0,graph_.bottom()+9,available,18),Qt::AlignRight|Qt::AlignVCenter,xLabel);p->restore();
        }
        else text(graph_.center().x(),height()-8,xLabel);
        if(matrix){
            double minimum=plot_.value("colorMin").toDouble(),maximum=plot_.value("colorMax").toDouble();
            if(!plot_.contains("colorMin")){minimum=INFINITY;maximum=-INFINITY;for(auto row:plot_["matrix"].toList())for(auto v:row.toList()){minimum=qMin(minimum,v.toDouble());maximum=qMax(maximum,v.toDouble());}}
            QLinearGradient gradient(graph_.right()+15,graph_.bottom(),graph_.right()+15,graph_.top());for(int i=0;i<=20;i++)gradient.setColorAt(i/20.,heat(i/20.));p->fillRect(QRectF(graph_.right()+14,graph_.top(),10,graph_.height()),gradient);text(graph_.right()+47,graph_.top()+4,number(maximum));text(graph_.right()+47,graph_.bottom()-4,number(minimum));
            p->save();p->translate(width()-5,graph_.center().y());p->rotate(-90);p->setPen(label);p->drawText(QRectF(-graph_.height()/2,-8,graph_.height(),16),Qt::AlignCenter,plot_["colorLabel"].toString());p->restore();
        }
    }
    p->save();p->translate(12,graph_.center().y());p->rotate(-90);p->setPen(label);p->drawText(QRectF(-graph_.height()/2,-9,graph_.height(),18),Qt::AlignCenter,plot_.value(kind=="waterfall"?"zLabel":"yLabel").toString());p->restore();
}


