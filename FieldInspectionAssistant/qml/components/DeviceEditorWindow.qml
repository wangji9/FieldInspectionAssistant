import QtQuick
import QtQuick.Window
import QtQuick.Controls
import "../theme"
Window {
    id:win;objectName:"deviceEditorWindow";visible:false;title:editingId?"编辑设备":"新增设备"
    property var ownerPage
    property string editingId:""
    readonly property int fixedWidth:Math.min(660,Screen.desktopAvailableWidth-32)
    readonly property int fixedHeight:Math.min(600,Screen.desktopAvailableHeight-40)
    width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight
    modality:Qt.WindowModal
    transientParent:ownerPage?ownerPage.Window.window:null
    flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
    color:Colors.background
    property alias form:editor
    function openDevice(value){editingId=value&&value.id?value.id:"";editor.open(win.title,[{key:"code",label:"设备编号",readOnly:!!editingId},{key:"name",label:"设备名称"},{key:"location",label:"安装位置"},{key:"model",label:"型号"}],value||{});show();raise();requestActivate()}
    ProductEditor{id:editor;onVisibleChanged:if(!visible&&win.visible)win.close();onSubmitted:function(v){var r=win.editingId?App.assets.updateAsset(win.editingId,v.name||"",v.location||"",v.model||""):App.assets.createAsset(v.code||"",v.name||"",v.location||"",v.model||"");if(!r.ok){message=r.error;return}if(win.ownerPage)win.ownerPage.reloadAssets(r.id||win.editingId);win.close()}}
    onClosing:{editor.visible=false;if(App.input.targetWindow===win)App.input.cancel()}
}
