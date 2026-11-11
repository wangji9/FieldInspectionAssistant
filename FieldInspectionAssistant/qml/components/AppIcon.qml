import QtQuick
import "../theme"

Canvas {
    id: root
    property string name: "sparkles"
    property color color: Colors.textSecondary
    property real strokeWidth: 1.7
    implicitWidth: 20; implicitHeight: 20
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var c = getContext("2d")
        c.reset(); c.scale(width / 24, height / 24)
        c.strokeStyle = root.color; c.fillStyle = root.color
        c.lineWidth = root.strokeWidth; c.lineCap = "round"; c.lineJoin = "round"
        function line(p, close) { c.beginPath(); c.moveTo(p[0],p[1]); for(var i=2;i<p.length;i+=2)c.lineTo(p[i],p[i+1]); if(close)c.closePath(); c.stroke() }
        function circle(x,y,r) { c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.stroke() }
        function box(x,y,w,h,r) { c.beginPath();c.moveTo(x+r,y);c.lineTo(x+w-r,y);c.quadraticCurveTo(x+w,y,x+w,y+r);c.lineTo(x+w,y+h-r);c.quadraticCurveTo(x+w,y+h,x+w-r,y+h);c.lineTo(x+r,y+h);c.quadraticCurveTo(x,y+h,x,y+h-r);c.lineTo(x,y+r);c.quadraticCurveTo(x,y,x+r,y);c.stroke() }
        switch(name) {
        case "grid": box(3,3,7,7,1.5);box(14,3,7,7,1.5);box(3,14,7,7,1.5);box(14,14,7,7,1.5);break
        case "tasks": box(5,4,14,17,2);box(9,2,6,4,1);line([9,11,15,11]);line([9,16,15,16]);break
        case "camera": line([3,7,7,7,9,4,15,4,17,7,21,7,21,20,3,20],true);circle(12,13,4);break
        case "devices": box(3,4,18,13,2);line([8,21,16,21]);line([12,17,12,21]);break
        case "alert": line([12,3,22,20,2,20],true);line([12,9,12,13]);circle(12,17,0.5);break
        case "archive": box(4,8,16,13,2);box(3,3,18,5,1);line([9,12,15,12]);break
        case "settings": circle(12,12,4);circle(12,12,8);line([12,1,12,4]);line([12,20,12,23]);line([1,12,4,12]);line([20,12,23,12]);break
        case "image": box(3,3,18,18,3);circle(8,8,1.5);line([3,17,9,11,14,16,17,13,21,17]);break
        case "chat": line([5,4,19,4,21,6,21,16,19,18,9,18,4,22,4,17,3,16,3,6],true);line([8,9,16,9]);line([8,13,13,13]);break
        case "voice": line([4,10,4,14]);line([8,6,8,18]);line([12,3,12,21]);line([16,7,16,17]);line([20,10,20,14]);break
        case "mic": box(9,3,6,12,3);c.beginPath();c.arc(12,11,7,0,Math.PI);c.stroke();line([12,18,12,22]);line([8,22,16,22]);break
        case "arrow-up": line([12,20,12,4]);line([6,10,12,4,18,10]);break
        case "arrow-right": line([4,12,20,12]);line([14,6,20,12,14,18]);break
        case "chevron-down": line([7,10,12,15,17,10]);break
        case "chevron-right": line([9,5,16,12,9,19]);break
        case "plus": line([12,4,12,20]);line([4,12,20,12]);break
        case "copy":box(8,8,12,13,2);line([15,8,15,3,3,3,3,16,8,16]);break
        case "check": line([5,12,10,17,19,7]);break
        case "close": line([6,6,18,18]);line([18,6,6,18]);break
        case "search": circle(10,10,6);line([15,15,21,21]);break
        case "play": line([8,4,20,12,8,20],true);break
        case "pause": line([9,5,9,19]);line([15,5,15,19]);break
        case "stop": box(5,5,14,14,3);break
        case "folder": line([3,6,9,6,11,9,21,9,21,20,3,20],true);break
        case "save": line([4,3,17,3,21,7,21,21,4,21],true);box(8,3,8,6,0);box(7,13,11,8,0);break
        case "video": box(3,5,13,14,2);line([16,9,22,6,22,18,16,15]);break
        case "upload": line([12,16,12,3]);line([7,8,12,3,17,8]);line([4,15,4,21,20,21,20,15]);break
        case "chart":line([3,3,3,21,22,21]);line([6,16,10,10,14,13,20,5]);break
        case "users":circle(9,7,3);circle(17,8,2.5);c.beginPath();c.arc(9,20,6,Math.PI,0);c.stroke();c.beginPath();c.arc(18,19,4,Math.PI,0);c.stroke();break
        default: line([12,2,15,9,22,12,15,15,12,22,9,15,2,12,9,9],true);line([20,2,20,6]);line([18,4,22,4]);break
        }
    }
}
