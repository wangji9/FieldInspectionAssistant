#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QPolygonF>
#include <QColor>
#include <QPointer>
class NativeHeatmapItem:public QQuickPaintedItem{
    Q_OBJECT
    Q_PROPERTY(QVariantMap plotData READ plotData WRITE setPlotData NOTIFY plotDataChanged)
    Q_PROPERTY(QObject *analysisSource READ analysisSource WRITE setAnalysisSource NOTIFY sourceChanged)
    Q_PROPERTY(QString analysisField READ analysisField WRITE setAnalysisField NOTIFY sourceChanged)
    Q_PROPERTY(QString plotTitle READ plotTitle NOTIFY plotDataChanged)
    Q_PROPERTY(bool matlabStyle READ matlabStyle WRITE setMatlabStyle NOTIFY styleChanged)
    Q_PROPERTY(bool darkStyle READ darkStyle WRITE setDarkStyle NOTIFY styleChanged)
    Q_PROPERTY(double rangeStart MEMBER rangeStart_ NOTIFY rangeChanged)
    Q_PROPERTY(double rangeEnd MEMBER rangeEnd_ NOTIFY rangeChanged)
    Q_PROPERTY(bool manualY MEMBER manualY_ NOTIFY rangeChanged)
    Q_PROPERTY(double yMinimum MEMBER yMinimum_ NOTIFY rangeChanged)
    Q_PROPERTY(double yMaximum MEMBER yMaximum_ NOTIFY rangeChanged)
public:
    explicit NativeHeatmapItem(QQuickItem *parent=nullptr);
    QVariantMap plotData()const{return plot_;}void setPlotData(const QVariantMap &plot);
    QObject *analysisSource()const{return source_;}void setAnalysisSource(QObject*);
    QString analysisField()const{return field_;}void setAnalysisField(const QString&);
    QString plotTitle()const{return plot_.value("title",QStringLiteral("等待真实采样")).toString();}
    bool matlabStyle()const{return matlabStyle_;}
    void setMatlabStyle(bool value);
    bool darkStyle() const { return darkStyle_; }
    void setDarkStyle(bool value);
    Q_INVOKABLE void refreshSource();
    Q_INVOKABLE QVariantMap renderMetrics()const{return {{"decode_ms",decodeMs_},{"paint_ms",paintMs_}};}
    void paint(QPainter *painter)override;
    Q_INVOKABLE QString coordinates(double x,double y)const;
    Q_INVOKABLE QPointF plotPosition(double x,double y)const{return QPointF(qBound(0.,(x-graph_.left())/qMax(1.,graph_.width()),1.),qBound(0.,(y-graph_.top())/qMax(1.,graph_.height()),1.));}
    Q_INVOKABLE QRectF plotGeometry()const{return graph_;}
signals:void plotDataChanged();void rangeChanged();void sourceChanged();void styleChanged();
private:QVariantMap plot_;QImage image_;double rangeStart_=0,rangeEnd_=1,yMinimum_=-1,yMaximum_=1;
    QVector<QPolygonF> linePoints_;QVector<QColor> lineColors_;QRectF dataBounds_;
    QPointer<QObject> source_;QString field_="plot";QMetaObject::Connection sourceConnection_;
    double decodeMs_=0,paintMs_=0;
    bool manualY_=false;QRectF graph_;double xmin_=0,xmax_=1,ymin_=0,ymax_=1;
    bool matlabStyle_=false;
    bool darkStyle_=false;
    QImage curveLayer_;
};
