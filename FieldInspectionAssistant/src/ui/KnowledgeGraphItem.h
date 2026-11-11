#pragma once
#include <QQuickPaintedItem>
#include <QTimer>
class KnowledgeGraphItem:public QQuickPaintedItem{
    Q_OBJECT
    Q_PROPERTY(QVariantMap graph READ graph WRITE setGraph NOTIFY graphChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList hits READ hits WRITE setHits NOTIFY graphChanged)
    Q_PROPERTY(int highlightRevision READ highlightRevision WRITE setHighlightRevision NOTIFY graphChanged)
    Q_PROPERTY(bool changesOnly READ changesOnly WRITE setChangesOnly NOTIFY graphChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool dynamicLayout READ dynamicLayout WRITE setDynamicLayout NOTIFY viewChanged)
public:
    KnowledgeGraphItem(QQuickItem *parent=nullptr);
    QVariantMap graph()const{return graph_;}void setGraph(const QVariantMap&);
    QString selectedId()const{return selected_;}void setSelectedId(const QString&);
    QVariantList hits()const{return hits_;}void setHits(const QVariantList&v){hits_=v;update();emit graphChanged();}
    int highlightRevision()const{return revision_;}void setHighlightRevision(int v){if(revision_==v)return;revision_=v;if(only_)layout();else update();emit graphChanged();}
    bool changesOnly()const{return only_;}void setChangesOnly(bool v){only_=v;layout();emit graphChanged();}
    qreal zoom()const{return zoom_;}void paint(QPainter*)override;
    bool dynamicLayout()const{return dynamic_;}void setDynamicLayout(bool);
    Q_INVOKABLE void fit();Q_INVOKABLE void zoomIn();Q_INVOKABLE void zoomOut();Q_INVOKABLE void focusNode(const QString&);
    Q_INVOKABLE QVariantMap layoutAudit()const;
signals:void graphChanged();void selectionChanged();void viewChanged();void nodeSelected(const QString&);
protected:
    void mousePressEvent(QMouseEvent*)override;void mouseMoveEvent(QMouseEvent*)override;void mouseReleaseEvent(QMouseEvent*)override;void wheelEvent(QWheelEvent*)override;
    void geometryChange(const QRectF&,const QRectF&)override;
private:
    void layout();void scaleAt(qreal,const QPointF&);QPointF world(const QPointF&p)const{return (p-pan_)/zoom_;}
    void simulate();void fitBounds();void updateBounds();
    QTimer simulation_;QMap<QString,QPointF> velocity_;int iteration_=0;bool dynamic_=true,autoFit_=true;QString draggedNode_;
    QVariantMap graph_;QVariantList hits_,nodes_,edges_;QMap<QString,QRectF> boxes_;QRectF bounds_;
    QString selected_;int revision_=0;bool only_=false,drag_=false;QPointF pan_,last_,pressed_;qreal zoom_=1;
};
