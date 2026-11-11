#include <QGuiApplication>
#include "KnowledgeGraphItem.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <utility>
KnowledgeGraphItem::KnowledgeGraphItem(QQuickItem*p):QQuickPaintedItem(p){setAntialiasing(true);setAcceptedMouseButtons(Qt::LeftButton);setClip(true);simulation_.setInterval(33);connect(&simulation_,&QTimer::timeout,this,&KnowledgeGraphItem::simulate);connect(this,&QQuickItem::visibleChanged,this,[this]{if(isVisible()&&dynamic_&&iteration_<180&&!boxes_.isEmpty())simulation_.start();else simulation_.stop();});}
void KnowledgeGraphItem::setGraph(const QVariantMap&g){
    if(graph_==g)return;
    auto shape=[](const QVariantMap &m){QStringList keys;for(auto v:m.value("nodes").toList())keys<<v.toMap().value("id").toString();for(auto v:m.value("edges").toList()){auto e=v.toMap();keys<<e.value("id").toString()+e.value("from").toString()+e.value("to").toString()+e.value("predicate").toString();}return keys;};
    bool same=!only_&&graph_.value("layout")==g.value("layout")&&shape(graph_)==shape(g);graph_=g;
    if(same){nodes_=g.value("nodes").toList();edges_=g.value("edges").toList();update();}else layout();emit graphChanged();
}
void KnowledgeGraphItem::setSelectedId(const QString&s){if(selected_==s)return;selected_=s;update();emit selectionChanged();}
void KnowledgeGraphItem::layout(){
    simulation_.stop();velocity_.clear();iteration_=0;
    nodes_.clear();edges_.clear();boxes_.clear();QSet<QString> ids;
    for(auto v:graph_["nodes"].toList()){auto n=v.toMap();if(only_&&revision_>0&&n["updated"].toInt()!=revision_)continue;nodes_.append(n);ids.insert(n["id"].toString());}
    for(auto v:graph_["edges"].toList()){auto e=v.toMap();if(ids.contains(e["from"].toString())&&ids.contains(e["to"].toString())&&(!only_||revision_<=0||e["updated"].toInt()==revision_))edges_.append(e);}
    if (!nodes_.isEmpty() && graph_.value("layout") == "layers") {
        QMap<int, QStringList> layers;
        for (const auto &value : std::as_const(nodes_)) {
            const auto node = value.toMap();
            layers[node.value("layer").toInt()].append(node.value("id").toString());
        }
        constexpr int rowsPerColumn = 5;
        constexpr qreal horizontalStep = 172, verticalStep = 120;
        qreal x = 0;
        for (const auto &layer : std::as_const(layers)) {
            for (qsizetype start = 0; start < layer.size(); start += rowsPerColumn) {
                const auto count = qMin<qsizetype>(rowsPerColumn, layer.size() - start);
                for (qsizetype row = 0; row < count; ++row) {
                    const qreal y = (row - (count - 1) * .5) * verticalStep;
                    boxes_[layer[start + row]] = QRectF(x - 48, y - 48, 96, 96);
                }
                x += horizontalStep;
            }
        }
        updateBounds();
        fit();
        return;
    }
    QMap<QString,QStringList> adjacency;for(auto v:edges_){auto e=v.toMap();auto a=e["from"].toString(),b=e["to"].toString();adjacency[a]<<b;adjacency[b]<<a;}
    QSet<QString> visited;QStringList ordered=ids.values();std::sort(ordered.begin(),ordered.end(),[&](const QString&a,const QString&b){return adjacency[a].size()==adjacency[b].size()?a<b:adjacency[a].size()>adjacency[b].size();});
    double nextX=40,maxBottom=160;
    // High-degree hub in the center, breadth-first neighbors on collision-free rings.
    for(auto id:ordered){if(visited.contains(id))continue;QStringList queue{id};QMap<QString,int> depth;QMap<int,QStringList> rings;visited.insert(id);depth[id]=0;
        for(int i=0;i<queue.size();i++){auto current=queue[i];int d=depth[current];rings[d]<<current;auto neighbors=adjacency[current];std::sort(neighbors.begin(),neighbors.end());for(auto neighbor:neighbors)if(!visited.contains(neighbor)){visited.insert(neighbor);depth[neighbor]=d+1;queue<<neighbor;}}
        QMap<QString,QPointF> positions;double radius=0;QRectF component;
        for(auto it=rings.begin();it!=rings.end();++it){int count=it.value().size();if(it.key()>0)radius=qMax(radius+210.,count*145./(2*M_PI));for(int i=0;i<count;i++){double angle=-M_PI/2+2*M_PI*i/qMax(1,count);auto center=it.key()==0?QPointF():QPointF(std::cos(angle)*radius,std::sin(angle)*radius);positions[it.value()[i]]=center;auto box=QRectF(center-QPointF(48,48),QSizeF(96,96));component=component.isNull()?box:component.united(box);}}
        QPointF offset(nextX-component.left(),50-component.top());for(auto it=positions.begin();it!=positions.end();++it)boxes_[it.key()]=QRectF(it.value()+offset-QPointF(48,48),QSizeF(96,96));nextX+=component.width()+125;maxBottom=qMax(maxBottom,component.height()+100);
    }
    bounds_=QRectF(0,0,qMax(220.,nextX-75),maxBottom);fit();if(dynamic_&&isVisible()&&boxes_.size()>1)simulation_.start();
}
void KnowledgeGraphItem::updateBounds(){QRectF b;for(auto box:boxes_)b=b.isNull()?box:b.united(box);bounds_=b.adjusted(-55,-55,55,55);}
void KnowledgeGraphItem::fitBounds(){zoom_=qBound(.04,qMin(width()/qMax(1.,bounds_.width()),height()/qMax(1.,bounds_.height())),1.2);pan_=QPointF(width()/2,height()/2)-bounds_.center()*zoom_;update();emit viewChanged();}
void KnowledgeGraphItem::fit(){autoFit_=true;fitBounds();}
void KnowledgeGraphItem::setDynamicLayout(bool value){if(dynamic_==value)return;dynamic_=value;if(value){iteration_=0;if(isVisible()&&boxes_.size()>1)simulation_.start();}else simulation_.stop();emit viewChanged();}
void KnowledgeGraphItem::simulate(){
    if(!dynamic_||!isVisible()||++iteration_>180){simulation_.stop();return;}
    auto ids=boxes_.keys();QMap<QString,QPointF> force;double cooling=qMax(.08,1.-iteration_/180.);
    for(int i=0;i<ids.size();++i)for(int j=i+1;j<ids.size();++j){auto delta=boxes_[ids[i]].center()-boxes_[ids[j]].center();double d=qMax(1.,std::hypot(delta.x(),delta.y()));if(d>600)continue;if(d<1.1)delta=QPointF(1,.5);auto f=delta/d*qMin(10.,15000./(d*d));force[ids[i]]+=f;force[ids[j]]-=f;}
    for(auto v:edges_){auto e=v.toMap();auto a=e["from"].toString(),b=e["to"].toString();if(a==b)continue;auto delta=boxes_[b].center()-boxes_[a].center();double d=qMax(1.,std::hypot(delta.x(),delta.y()));auto f=delta/d*qBound(-8.,(d-205.)*.022,8.);force[a]+=f;force[b]-=f;}
    for(auto id:ids){if(id==draggedNode_)continue;auto v=(velocity_[id]+force[id]*cooling)*.72;double speed=std::hypot(v.x(),v.y());if(speed>9)v*=9/speed;velocity_[id]=v;boxes_[id].translate(v);}
    // Collision constraints protect circular nodes even while the simulation is moving.
    for(int pass=0;pass<3;++pass)for(int i=0;i<ids.size();++i)for(int j=i+1;j<ids.size();++j){auto delta=boxes_[ids[i]].center()-boxes_[ids[j]].center();double d=std::hypot(delta.x(),delta.y());if(d>=142)continue;if(d<.1){delta=QPointF(1,0);d=1;}auto shift=delta/d*(142-d)*.51;if(ids[i]!=draggedNode_)boxes_[ids[i]].translate(shift);if(ids[j]!=draggedNode_)boxes_[ids[j]].translate(-shift);}
    updateBounds();if(autoFit_&&iteration_%3==0)fitBounds();else update();
}
void KnowledgeGraphItem::geometryChange(const QRectF&n,const QRectF&o){QQuickPaintedItem::geometryChange(n,o);if(n.size()!=o.size())fit();}
void KnowledgeGraphItem::scaleAt(qreal factor,const QPointF&p){autoFit_=false;auto w=world(p);zoom_=qBound(.04,zoom_*factor,2.5);pan_=p-w*zoom_;update();emit viewChanged();}
void KnowledgeGraphItem::zoomIn(){scaleAt(1.25,QPointF(width()/2,height()/2));}void KnowledgeGraphItem::zoomOut(){scaleAt(.8,QPointF(width()/2,height()/2));}
void KnowledgeGraphItem::focusNode(const QString&id){setSelectedId(id);if(!boxes_.contains(id))return;autoFit_=false;zoom_=qMax(.85,zoom_);pan_=QPointF(width()/2,height()/2)-boxes_[id].center()*zoom_;update();emit viewChanged();}
void KnowledgeGraphItem::mousePressEvent(QMouseEvent*e){last_=pressed_=e->position();drag_=true;draggedNode_.clear();auto point=world(e->position());for(auto it=boxes_.begin();it!=boxes_.end();++it)if(QLineF(it.value().center(),point).length()<=48){draggedNode_=it.key();break;}e->accept();}
void KnowledgeGraphItem::mouseMoveEvent(QMouseEvent*e){if(!drag_)return;autoFit_=false;auto delta=e->position()-last_;if(draggedNode_.isEmpty())pan_+=delta;else{boxes_[draggedNode_].translate(delta/zoom_);velocity_[draggedNode_]={};updateBounds();}last_=e->position();update();}
void KnowledgeGraphItem::mouseReleaseEvent(QMouseEvent*e){drag_=false;if(QLineF(pressed_,e->position()).length()<6){auto point=world(e->position());for(auto it=boxes_.begin();it!=boxes_.end();++it)if(QLineF(it.value().center(),point).length()<=48){setSelectedId(it.key());emit nodeSelected(it.key());break;}}draggedNode_.clear();e->accept();}
void KnowledgeGraphItem::wheelEvent(QWheelEvent*e){scaleAt(e->angleDelta().y()>0?1.15:1/1.15,e->position());e->accept();}
QVariantMap KnowledgeGraphItem::layoutAudit()const{int overlaps=0;auto rects=boxes_.values();for(int i=0;i<rects.size();i++)for(int j=i+1;j<rects.size();j++)if(rects[i].intersects(rects[j]))++overlaps;return {{"nodes",boxes_.size()},{"overlaps",overlaps},{"zoom",zoom_},{"width",bounds_.width()},{"height",bounds_.height()},{"iterations",iteration_},{"animating",simulation_.isActive()}};}
void KnowledgeGraphItem::paint(QPainter*p){
    p->fillRect(boundingRect(),QColor("#FBFCFF"));if(nodes_.isEmpty()){p->setPen(QColor("#7E879D"));QFont f=QGuiApplication::font();f.setPixelSize(16);p->setFont(f);p->drawText(boundingRect(),Qt::AlignCenter,only_?QStringLiteral("此版本没有匹配的实体"):QStringLiteral("导入资料或对话，构建知识图谱"));return;}
    p->setRenderHint(QPainter::Antialiasing);p->translate(pan_);p->scale(zoom_,zoom_);QFont f=QGuiApplication::font();f.setPixelSize(14);p->setFont(f);QList<QRectF> labels;QMap<QString,int> lanes;
    auto color=[&](const QVariantMap&m){if(m.contains("selected")&&!m["selected"].toBool())return QColor("#AEB6C7");if(revision_>0&&m["created"].toInt()==revision_)return QColor("#159B77");if(revision_>0&&m["updated"].toInt()==revision_)return QColor("#7760DA");return QColor("#9CA7BD");};
    for(auto v:edges_){auto e=v.toMap();p->setOpacity(e.value("selected",true).toBool()?1.:.28);auto a=boxes_[e["from"].toString()],b=boxes_[e["to"].toString()];auto delta=b.center()-a.center();double length=std::hypot(delta.x(),delta.y());QPointF unit=length>0?delta/length:QPointF(1,0),normal(-unit.y(),unit.x());auto start=a.center()+unit*51,end=b.center()-unit*53;QPainterPath path(start);
        if(a==b){start=a.center()+QPointF(34,-34);end=a.center()+QPointF(-34,-34);path=QPainterPath(start);path.cubicTo(a.right()+70,a.top()-80,a.left()-70,a.top()-80,end.x(),end.y());}else{auto key=e["from"].toString()<e["to"].toString()?e["from"].toString()+e["to"].toString():e["to"].toString()+e["from"].toString();int lane=lanes[key]++;double offset=lane==0?0:((lane+1)/2)*28*(lane%2?1:-1);auto mid=(start+end)/2+normal*offset;path.quadTo(mid,end);}
        p->setPen(QPen(color(e),1.8));p->setBrush(Qt::NoBrush);p->drawPath(path);auto before=path.pointAtPercent(.97);double angle=std::atan2(end.y()-before.y(),end.x()-before.x());QPolygonF arrow;arrow<<end<<end-QPointF(std::cos(angle-.45)*9,std::sin(angle-.45)*9)<<end-QPointF(std::cos(angle+.45)*9,std::sin(angle+.45)*9);p->setBrush(color(e));p->drawPolygon(arrow);
        QString title=e["predicate"].toString();double w=p->fontMetrics().horizontalAdvance(title)+14;QRectF chosen;for(double t:{.5,.35,.65,.2,.8}){auto c=path.pointAtPercent(t);QRectF box(c.x()-w/2,c.y()-11,w,22);bool clash=false;for(auto r:boxes_)if(r.adjusted(-5,-5,5,5).intersects(box)){clash=true;break;}for(auto r:labels)if(r.adjusted(-3,-3,3,3).intersects(box))clash=true;if(!clash){chosen=box;break;}}
        if(!chosen.isNull()){labels<<chosen;p->setBrush(QColor("#FFFFFF"));p->setPen(QPen(QColor("#E1E6F0"),1));p->drawRoundedRect(chosen,5,5);p->setPen(QColor("#626C83"));p->drawText(chosen,Qt::AlignCenter,title);}
    }
    const QStringList palette{"#FFD0A1","#A3CDF8","#A4E1C7","#C9B7ED","#F6B8C8","#AAE1E6","#F1DA94"};
    const QMap<QString,QString> typeColors{{"设备","#FFD0A1"},{"部件","#A3CDF8"},{"缺陷","#F6B8C8"},{"位置","#A4E1C7"},{"指标","#C9B7ED"},{"巡检措施","#AAE1E6"}};
    for(auto v:nodes_){auto n=v.toMap();p->setOpacity(n.value("selected",true).toBool()?1.:.30);auto id=n["id"].toString();auto rect=boxes_[id];bool selected=id==selected_,hit=hits_.contains(id);quint32 hash=2166136261u;auto type=n["type"].toString();for(auto ch:type)hash=(hash^ch.unicode())*16777619u;auto c=color(n),fill=QColor(typeColors.value(type,palette[hash%palette.size()]));p->setPen(QPen(selected?QColor("#6558EC"):hit?QColor("#ECA143"):c,selected||hit?3.5:2.2));p->setBrush(Qt::NoBrush);p->drawEllipse(rect.adjusted(-4,-4,4,4));p->setPen(QPen(fill.darker(135),1));p->setBrush(fill);p->drawEllipse(rect);
        f.setPixelSize(16);f.setBold(true);p->setFont(f);p->setPen(QColor("#263148"));QString name=n["name"].toString();QStringList lines;while(!name.isEmpty()&&lines.size()<2){int count=1;while(count<name.size()&&p->fontMetrics().horizontalAdvance(name.left(count+1))<=78)++count;lines<<name.left(count);name.remove(0,count);}if(!name.isEmpty())lines.last()=p->fontMetrics().elidedText(lines.last()+name,Qt::ElideRight,78);p->drawText(rect.adjusted(8,12,-8,-28),Qt::AlignCenter,lines.join('\n'));f.setPixelSize(14);f.setBold(false);p->setFont(f);p->setPen(QColor("#526078"));p->drawText(rect.adjusted(10,64,-10,-12),Qt::AlignCenter,p->fontMetrics().elidedText(n["type"].toString(),Qt::ElideRight,76));
    }
}
