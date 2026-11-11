import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FieldInspection
import "../theme"
import "../components"
Rectangle {
    id:root;objectName:"cameraCapturePage";color:Colors.background
    property var devices:[]
    property var formats:[]
    property string message:""
    property string pendingReturn:""
    property bool controlsVisible:false
    property string selectedVideoFps:"15"
    readonly property bool compact:width<680
    readonly property bool recordingLocked:App.camera.recording||App.camera.finalizing
    readonly property bool cameraChanging:App.camera.opening||App.camera.closing
    readonly property bool mediaBusy:recordingLocked||App.camera.photoSaving
    readonly property bool canConfigure:!App.evidence.cameraConnected&&!cameraChanging&&!mediaBusy
    readonly property bool canCapture:App.evidence.cameraConnected&&App.camera.hasFrame&&!cameraChanging&&!mediaBusy
    readonly property bool canUsePhoto:App.camera.lastPhoto.length>0&&!mediaBusy&&!cameraChanging
    readonly property var videoRates:{var limit=App.evidence.cameraConnected?App.camera.fps:Number(captureFps.currentText)||30;var rates=[10,15,30].filter(function(v){return v<=limit}).map(String);return rates.length?rates:[String(Math.max(1,limit))]}
    signal photoForQuestion(string path)
    signal projectCapture(string path, string kind)
    function refreshCameras(){
        if(!canConfigure)return
        var previous=cameraChoice.currentText;devices=App.evidence.cameras();cameraChoice.currentIndex=devices.length?Math.max(0,devices.indexOf(previous)):-1;reloadFormats()
    }
    function reloadFormats(){
        var previous=resolutionChoice.currentText||App.camera.resolution
        formats=cameraChoice.currentIndex>=0?App.camera.resolutions(devices[cameraChoice.currentIndex]):[]
        var index=formats.indexOf(previous);if(index<0)index=formats.indexOf("1280×720");resolutionChoice.currentIndex=index>=0?index:formats.length?0:-1
        message=devices.length&&!formats.length?"此摄像头没有可用的 MJPEG 格式，请切换设备":""
    }
    function openCamera(){
        if(!canConfigure)return
        if(cameraChoice.currentIndex<0){message="未检测到摄像头，请连接后刷新";return}
        var size=resolutionChoice.currentText.split("×")
        if(size.length!==2){message="请选择可用的摄像头格式";return}
        if(!App.camera.configure(Number(size[0]),Number(size[1]),Number(captureFps.currentText))){message=App.camera.status;return}
        message=App.evidence.openCamera(devices[cameraChoice.currentIndex])?"":App.camera.status
        if(compact)controlsVisible=false
    }
    function toggleCamera(){if(cameraChanging||mediaBusy)return;if(App.evidence.cameraConnected)App.evidence.closeCamera();else openCamera()}
    function savePhoto(){if(!canCapture)return;if(!App.camera.savePhotoAsync(photoFormat.currentText,Number(photoQuality.currentText)))message=App.camera.status}
    function toggleVideo(){
        if(App.camera.recording){App.camera.stopVideo();return}
        if(!canCapture)return
        if(!App.camera.startVideo(Number(videoFps.currentText),[60,300,600][videoLimit.currentIndex],[4,2,8][videoQuality.currentIndex]))message=App.camera.status
    }
    function openPhotoQuestion(){
        if(!canUsePhoto||App.ai.busy||App.dictation.active||App.speech.recording)return
        var path=App.camera.photoForQuestion();if(path.length)photoForQuestion(path)
    }
    function linkEvidence(){if(!canUsePhoto||App.workspace.busy)return;var path=App.camera.photoForQuestion();if(path.length)App.nav.push("product",{section:"evidence",path:path,kind:"photo"})}
    function setDirectory(path){App.camera.saveDirectory=path;savePath.text=Qt.binding(function(){return App.camera.saveDirectory})}
    function returnToDashboard(){
        if(App.camera.photoSaving||App.camera.finalizing||cameraChanging)return
        if(App.camera.recording){pendingReturn="dashboard";App.camera.stopVideo();return}
        App.nav.push("dashboard")
    }
    function finishReturn(){if(pendingReturn.length&&!mediaBusy&&!cameraChanging){var page=pendingReturn;pendingReturn="";App.nav.push(page)}}
    Component.onCompleted:refreshCameras()
    onVisibleChanged:{if(visible){if(!App.evidence.cameraConnected)refreshCameras();message=""}else{pendingReturn="";if(App.evidence.cameraConnected||App.camera.opening)App.evidence.closeCamera()}}
    Connections{
        target:App.camera
        function onError(error){root.message=error;root.pendingReturn=""}
        function onStatusChanged(){root.message=""}
        function onRecordingChanged(){root.finishReturn()}
        function onClosed(){if(root.visible&&root.canConfigure)root.refreshCameras()}
    }
    FolderDialog{id:folderDialog;title:"选择照片与录像保存目录";currentFolder:"file://"+App.camera.saveDirectory;onAccepted:root.setDirectory(selectedFolder.toString())}
    ColumnLayout{
        anchors.fill:parent;anchors.margins:12;spacing:10
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:34;spacing:10
            Text{text:"现场拍照";font.pixelSize:Typography.title;color:Colors.text}
            Item{Layout.fillWidth:true}
            TouchButton{objectName:"captureControlsToggle";visible:root.compact;text:root.controlsVisible?"收起":"控制";autoWidth:true;height:32;variant:"ghost";onClicked:root.controlsVisible=!root.controlsVisible}
            TouchButton{objectName:"captureBackButton";text:root.pendingReturn.length?"保存后返回…":App.camera.recording?"停止并返回":root.compact?"返回":"返回工作台";autoWidth:true;height:32;variant:"ghost";enabled:!root.cameraChanging&&!App.camera.photoSaving&&!App.camera.finalizing;onClicked:root.returnToDashboard()}
        }
        Item{
            id:content;objectName:"captureContent";Layout.fillWidth:true;Layout.fillHeight:true
            Rectangle{
                id:cameraStage;objectName:"capturePreviewPane";anchors.left:parent.left;anchors.top:parent.top;anchors.bottom:parent.bottom
                anchors.right:root.compact?parent.right:settingsPane.left;anchors.rightMargin:root.compact?0:12
                color:"#242424";clip:true
                CameraPreview{
                    id:cameraPreview;anchors.fill:parent;anchors.margins:2;visible:App.evidence.cameraConnected
                    onWidthChanged:App.camera.setPreviewSize(Math.round(width),Math.round(height))
                    onHeightChanged:App.camera.setPreviewSize(Math.round(width),Math.round(height))
                    Connections{target:App.camera;function onFrameReady(frame){cameraPreview.frame=frame}}
                }
                Column{
                    anchors.centerIn:parent;width:Math.max(60,Math.min(400,parent.width-32));spacing:16;visible:!App.evidence.cameraConnected
                    Text{width:parent.width;text:App.camera.opening?"正在打开摄像头…":App.camera.closing?"正在关闭摄像头…":"摄像头未打开";horizontalAlignment:Text.AlignHCenter;wrapMode:Text.Wrap;font.pixelSize:18;color:"#E3E6F4"}
                }
                Rectangle{
                    visible:App.evidence.cameraConnected||App.camera.recording;anchors.left:parent.left;anchors.top:parent.top;anchors.margins:14
                    width:liveLabel.implicitWidth+24;height:30;radius:10;color:"#CC24313A"
                    Text{id:liveLabel;anchors.centerIn:parent;text:App.camera.recording?"● REC  "+App.camera.recordingSeconds+" s":"● 实时画面";color:App.camera.recording?"#FF7A72":"#8EE1C1";font.pixelSize:Typography.body}
                }
            }
            Rectangle{anchors.fill:parent;visible:root.compact&&root.controlsVisible;z:1;color:"#50000000";MouseArea{anchors.fill:parent;onClicked:root.controlsVisible=false}}
            Rectangle{
                id:settingsPane;objectName:"captureSettingsPane";anchors.top:parent.top;anchors.bottom:parent.bottom;anchors.right:parent.right
                width:root.compact?Math.min(320,parent.width-24):Math.max(270,Math.min(340,root.width*.28));visible:!root.compact||root.controlsVisible;z:2
                color:Colors.panel;border.color:Colors.border
                ColumnLayout{
                    anchors.fill:parent;anchors.margins:14;spacing:10
                    RowLayout{
                        Layout.fillWidth:true;spacing:8
                        TouchButton{objectName:"connectCaptureCamera";Layout.fillWidth:true;height:38;text:App.camera.opening?"打开中…":App.camera.closing?"关闭中…":App.evidence.cameraConnected?"关闭摄像头":"打开摄像头";variant:App.evidence.cameraConnected?"tonal":"primary";enabled:!root.cameraChanging&&!root.mediaBusy&&(App.evidence.cameraConnected||root.devices.length>0&&root.formats.length>0);onClicked:root.toggleCamera()}
                        TouchButton{objectName:"refreshCaptureDevices";text:"刷新";width:62;height:38;variant:"ghost";enabled:root.canConfigure;onClicked:root.refreshCameras()}
                    }
                    RowLayout{
                        Layout.fillWidth:true;spacing:8
                        TouchButton{objectName:"saveCapturePhoto";Layout.fillWidth:true;height:40;text:App.camera.photoSaving?"保存中…":"拍照保存";variant:"primary";enabled:root.canCapture;onClicked:root.savePhoto()}
                        TouchButton{objectName:"captureVideoButton";Layout.fillWidth:true;height:40;text:App.camera.recording?"停止并保存":App.camera.finalizing?"保存中…":"开始录像";variant:App.camera.recording?"danger":"tonal";enabled:App.camera.recording||root.canCapture;onClicked:root.toggleVideo()}
                    }
                    Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                    ScrollView{
                        id:settingsScroll;objectName:"captureSettingsScroll";Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                        ScrollBar.horizontal.policy:ScrollBar.AlwaysOff
                        ColumnLayout{
                            width:settingsScroll.availableWidth;spacing:10
                            Text{text:"摄像头";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                            AppComboBox{id:cameraChoice;objectName:"captureCameraChoice";Layout.fillWidth:true;height:36;model:root.devices;enabled:root.canConfigure;font.pixelSize:Typography.body;displayText:currentIndex<0?"未检测到摄像头":currentText;onActivated:root.reloadFormats()}
                            RowLayout{Layout.fillWidth:true;spacing:8
                                AppComboBox{id:resolutionChoice;objectName:"captureResolution";Layout.fillWidth:true;Layout.preferredWidth:150;height:36;model:root.formats;enabled:root.canConfigure;font.pixelSize:Typography.body}
                                AppComboBox{id:captureFps;objectName:"capturePreviewFps";Layout.preferredWidth:92;height:36;model:["30","15","10"];displayText:currentText+" fps";enabled:root.canConfigure;font.pixelSize:Typography.body}
                            }
                            RowLayout{Layout.fillWidth:true;spacing:8
                                TouchButton{objectName:"captureRotate";text:"旋转 "+App.camera.rotation+"°";Layout.fillWidth:true;height:34;variant:"ghost";enabled:!root.mediaBusy&&!root.cameraChanging;onClicked:App.camera.rotation=(App.camera.rotation+90)%360}
                                AppCheckBox{objectName:"captureMirror";text:"镜像";checked:App.camera.mirrored;enabled:!root.mediaBusy&&!root.cameraChanging;font.pixelSize:Typography.body;onToggled:App.camera.mirrored=checked}
                            }
                            Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                            Text{text:"照片";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                            RowLayout{Layout.fillWidth:true;spacing:8
                                AppComboBox{id:photoFormat;objectName:"capturePhotoFormat";Layout.preferredWidth:82;height:36;model:["JPG","PNG"];enabled:!root.mediaBusy;font.pixelSize:Typography.body}
                                AppComboBox{id:photoQuality;objectName:"capturePhotoQuality";Layout.fillWidth:true;height:36;model:["92","80","100"];displayText:photoFormat.currentText==="PNG"?"PNG 无损":"质量 "+currentText;enabled:photoFormat.currentText==="JPG"&&!root.mediaBusy;font.pixelSize:Typography.body}
                            }
                            TouchButton{objectName:"capturePhotoQuestion";Layout.fillWidth:true;height:34;text:"用于图片问答";variant:"ghost";enabled:root.canUsePhoto&&!App.ai.busy&&!App.dictation.active&&!App.speech.recording;onClicked:root.openPhotoQuestion()}
                            TouchButton{objectName:"captureLinkEvidence";Layout.fillWidth:true;height:34;text:"关联巡检证据";variant:"ghost";enabled:root.canUsePhoto&&!App.workspace.busy;onClicked:root.linkEvidence()}
                            TouchButton{Layout.fillWidth:true;height:34;text:"照片归入工程";variant:"ghost";enabled:root.canUsePhoto&&!!App.projects.project.id;onClicked:root.projectCapture(App.camera.lastPhoto,"image")}
                            Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                            RowLayout{Layout.fillWidth:true
                                Text{text:"录像";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                                Item{Layout.fillWidth:true}
                                Text{text:"MP4 · 无声";font.pixelSize:Typography.body;color:Colors.textSecondary}
                            }
                            RowLayout{Layout.fillWidth:true;spacing:8
                                AppComboBox{id:videoFps;objectName:"captureVideoFps";Layout.preferredWidth:100;height:36;model:root.videoRates;currentIndex:Math.max(0,root.videoRates.indexOf(root.selectedVideoFps));displayText:currentText+" fps";enabled:!root.mediaBusy;font.pixelSize:Typography.body;onActivated:root.selectedVideoFps=currentText}
                                AppComboBox{id:videoQuality;objectName:"captureVideoQuality";Layout.fillWidth:true;height:36;model:["标准画质","高画质","节省空间"];enabled:!root.mediaBusy;font.pixelSize:Typography.body}
                            }
                            AppComboBox{id:videoLimit;objectName:"captureVideoLimit";Layout.fillWidth:true;height:36;model:["最长 1 分钟","最长 5 分钟","最长 10 分钟"];currentIndex:1;enabled:!root.mediaBusy;font.pixelSize:Typography.body}
                            TouchButton{Layout.fillWidth:true;height:34;text:"录像归入工程";variant:"ghost";enabled:!root.mediaBusy&&!!App.camera.lastVideo&&!!App.projects.project.id;onClicked:root.projectCapture(App.camera.lastVideo,"video")}
                            Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                            Text{text:"保存位置";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                            FieldInput{id:savePath;objectName:"captureSavePath";Layout.fillWidth:true;height:36;text:App.camera.saveDirectory;readOnly:root.mediaBusy;onEditingFinished:function(value){root.setDirectory(value)}}
                            RowLayout{Layout.fillWidth:true;spacing:8
                                TouchButton{objectName:"captureChooseDirectory";Layout.fillWidth:true;text:"选择目录";height:34;variant:"ghost";enabled:!root.mediaBusy;onClicked:folderDialog.open()}
                                TouchButton{text:"打开目录";Layout.fillWidth:true;height:34;variant:"ghost";onClicked:App.camera.openSaveDirectory()}
                            }
                            Item{Layout.fillWidth:true;height:4}
                        }
                    }
                    Text{objectName:"captureStatus";Layout.fillWidth:true;visible:text.length>0;text:root.message||App.camera.status;wrapMode:Text.Wrap;maximumLineCount:3;elide:Text.ElideMiddle;font.pixelSize:Typography.body;color:root.message?Colors.danger:Colors.textSecondary}
                }
            }
        }
    }
}
