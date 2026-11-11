import QtQuick
import QtQuick.Controls
import FieldInspection
import "../theme"

Rectangle {
    id: chart
    objectName: "signalChart"
    property var plot: ({})
    property var analysisSource:null
    property string analysisField:"plot"
    property bool showTitle:true
    property bool matlabStyle:true
    property real startFraction: 0
    property real endFraction: 1
    property bool manualY: false
    property real yMinimum: -1
    property real yMaximum: 1
    property real zoomY:1
    property real zoomX:1
    property real centerX:.5
    property real centerY:.5
    property bool aiSelectable:false
    property bool aiSelected:false
    signal aiSelectRequested()
    function analysisContext(){
        var p=nativePlot.plotData,out={title:p.title||nativePlot.plotTitle,kind:p.kind||"",x_label:p.x_label||p.xLabel||"",y_label:p.y_label||p.yLabel||"",sample_rate:p.sample_rate,metrics:p.metrics||{},viewport:{start:startFraction,end:endFraction,zoomX:zoomX,zoomY:zoomY,yMin:manualY?yMinimum:undefined,yMax:manualY?yMaximum:undefined},summary_only:true}
        for(var key in p)if(key!=="points"&&key!=="series"&&key!=="matrix"&&key!=="envelope"&&JSON.stringify(p[key]).length<2000)out[key]=p[key]
        function sample(points){var result=[];if(!points)return result;var step=Math.max(1,Math.ceil(points.length/12));for(var i=0;i<points.length;i+=step)result.push(points[i]);return result}
        out.points_sample=sample(p.points);out.series_sample=(p.series||[]).map(function(s){return {name:s.name,point_count:(s.points||[]).length,points:sample(s.points)}})
        if(p.matrix)out.matrix_size=[p.matrix.length,p.matrix.length?p.matrix[0].length:0]
        return out
    }
    AppCheckBox{z:10;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:4;visible:chart.aiSelectable;checked:chart.aiSelected;text:"选图";onToggled:chart.aiSelectRequested()}
    property bool selected:false
    signal selectedRequested()
    signal expandRequested()
    function resetZoom(){zoomY=1;zoomX=1;centerX=.5;centerY=.5}
    function boundedCenter(value,zoom){return Math.max(.5/zoom,Math.min(1-.5/zoom,value))}
    function zoomAt(factor,x,y,both){
        factor=Math.max(.05,Math.min(20,factor));x=Math.max(0,Math.min(1,x));y=1-Math.max(0,Math.min(1,y))
        var next=Math.max(1,Math.min(256,zoomX*factor)),anchor=centerX+(x-.5)/zoomX
        centerX=boundedCenter(anchor+(.5-x)/next,next);zoomX=next
        if(both&&manualY){next=Math.max(1,Math.min(1000,zoomY*factor));anchor=centerY+(y-.5)/zoomY;centerY=boundedCenter(anchor+(.5-y)/next,next);zoomY=next}
    }
    function panBy(dx,dy){var g=nativePlot.plotGeometry();centerX=boundedCenter(centerX-dx/Math.max(1,g.width)/zoomX,zoomX);if(manualY)centerY=boundedCenter(centerY+dy/Math.max(1,g.height)/zoomY,zoomY)}
    property string hoverText: ""
    color: Colors.panel; radius: matlabStyle?3:12; border.color: selected?"#7CA2D2":matlabStyle?"#CED3D9":Colors.border
    focus:true
    Keys.onPressed:function(event){if(event.key===Qt.Key_0){resetZoom();event.accepted=true}else if(event.key===Qt.Key_F||event.key===Qt.Key_Escape){expandRequested();event.accepted=true}}
    implicitHeight: 330
    signal imageSaved(bool ok)
    function savePng(path) { chart.grabToImage(function(result) { chart.imageSaved(result.saveToFile(path)) }) }
    Accessible.name:nativePlot.plotTitle
    Text { visible:chart.showTitle;x:16;y:14;width:chart.hoverText?parent.width*.45:parent.width-32;text:nativePlot.plotTitle;font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text;elide:Text.ElideRight }
    Text { anchors.right:parent.right;anchors.rightMargin:16;y:14;width:parent.width*.5;visible:chart.showTitle&&chart.hoverText.length>0;text:chart.hoverText;font.pixelSize:Typography.body;font.family:Typography.uiFamily;color:Colors.textSecondary;elide:Text.ElideRight;horizontalAlignment:Text.AlignRight }
    NativeHeatmap {
        id: nativePlot
        objectName: "nativeSignalPlot"
        x:6;y:chart.showTitle?40:6;width:parent.width-12;height:parent.height-y-6
        plotData:chart.analysisSource?({}):chart.visible?chart.plot:({})
        analysisSource:chart.analysisSource
        analysisField:chart.analysisField
        matlabStyle:chart.matlabStyle
        Component.onCompleted:refreshSource()
        rangeStart:chart.startFraction+(chart.endFraction-chart.startFraction)*(chart.centerX-.5/chart.zoomX);rangeEnd:chart.startFraction+(chart.endFraction-chart.startFraction)*(chart.centerX+.5/chart.zoomX)
        manualY:chart.manualY;yMinimum:chart.yMinimum+(chart.yMaximum-chart.yMinimum)*(chart.centerY-.5/chart.zoomY);yMaximum:chart.yMinimum+(chart.yMaximum-chart.yMinimum)*(chart.centerY+.5/chart.zoomY)
        TapHandler {onTapped:{chart.selectedRequested();chart.forceActiveFocus()}onDoubleTapped:chart.expandRequested()}
        PinchHandler {
            id:pinch;objectName:"signalChartPinch";target:null;minimumPointCount:2;maximumPointCount:2
            minimumScale:.00001;maximumScale:100000;minimumRotation:0;maximumRotation:0
            property real lastScale:1
            property point lastTranslation:Qt.point(0,0)
            onActiveChanged:if(active){lastScale=1;lastTranslation=Qt.point(translation.x,translation.y);chart.selectedRequested()}
            onUpdated:if(active){var pos=nativePlot.plotPosition(centroid.position.x,centroid.position.y);chart.zoomAt(activeScale/lastScale,pos.x,pos.y,true);chart.panBy(translation.x-lastTranslation.x,translation.y-lastTranslation.y);lastScale=activeScale;lastTranslation=Qt.point(translation.x,translation.y)}
        }
        DragHandler {
            id:drag;target:null;minimumPointCount:1;maximumPointCount:1;enabled:chart.zoomX>1||chart.manualY&&chart.zoomY>1
            property point previous:Qt.point(0,0)
            onActiveChanged:if(active){previous=Qt.point(0,0);chart.selectedRequested()}
            onTranslationChanged:if(active){chart.panBy(activeTranslation.x-previous.x,activeTranslation.y-previous.y);previous=Qt.point(activeTranslation.x,activeTranslation.y)}
        }
        MouseArea {
            anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.NoButton
            onPositionChanged:function(mouse){chart.hoverText=nativePlot.coordinates(mouse.x,mouse.y)}
            onExited:chart.hoverText=""
            ToolTip.visible:!chart.showTitle&&containsMouse&&chart.hoverText.length>0
            ToolTip.text:chart.hoverText
            ToolTip.delay:500
            onWheel:function(wheel){var pos=nativePlot.plotPosition(wheel.x,wheel.y),factor=wheel.angleDelta.y>0?1.25:.8;chart.selectedRequested();if((wheel.modifiers&Qt.ControlModifier)&&chart.manualY){var old=chart.zoomX,cx=chart.centerX;chart.zoomAt(factor,pos.x,pos.y,true);chart.zoomX=old;chart.centerX=cx}else chart.zoomAt(factor,pos.x,pos.y,false);wheel.accepted=true}
        }
    }
}
