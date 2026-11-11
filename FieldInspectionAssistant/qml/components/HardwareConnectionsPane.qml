import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Item {
    id:root;property var panel
    readonly property var status:App.hardware.snapshot
    readonly property var cameras:status.cameras||[]
    RowLayout{anchors.fill:parent;spacing:12
        Rectangle{Layout.preferredWidth:Math.max(280,root.width*.46);Layout.fillHeight:true;color:Colors.panel;border.color:Colors.border
            ScrollView{id:scroll;anchors.fill:parent;anchors.margins:16;clip:true;contentWidth:availableWidth
                ColumnLayout{width:scroll.availableWidth;spacing:14
                    RowLayout{Layout.fillWidth:true;Text{Layout.fillWidth:true;text:"硬件连接";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}TouchButton{objectName:"refreshHardwareDevices";text:"刷新设备";autoWidth:true;height:32;enabled:!App.hardware.busy;variant:"tonal";onClicked:App.hardware.refresh()}}
                    Text{text:"摄像头";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    AppComboBox{id:camera;objectName:"hardwareCameraChoice";Layout.fillWidth:true;model:root.cameras;textRole:"name";displayText:currentIndex<0?"未检测到摄像头":(currentText+" · "+model[currentIndex].path)}
                    RowLayout{Layout.fillWidth:true;TouchButton{text:"连接";height:34;autoWidth:true;enabled:camera.currentIndex>=0;variant:"primary";onClicked:root.panel.openCameraAt(root.cameras[camera.currentIndex].path)}TouchButton{text:"断开";height:34;autoWidth:true;enabled:App.evidence.cameraConnected;variant:"ghost";onClicked:App.evidence.closeCamera()}}
                    Text{Layout.fillWidth:true;text:App.camera.status||root.panel.cameraMessage||(camera.currentIndex<0?"未检测到摄像头":root.cameras[camera.currentIndex].accessible?"设备可访问 · 未打开":"已连接 · 权限不足");font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}
                    Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                    RowLayout{Layout.fillWidth:true;Text{Layout.fillWidth:true;text:"麦克风";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}TouchButton{objectName:"openMicrophoneSettings";text:"采集设置";autoWidth:true;height:30;variant:"ghost";onClicked:App.nav.push("analysis",{open_capture_settings:true})}}
                    Text{visible:!!root.status.audio_note;Layout.fillWidth:true;text:root.status.audio_note||"";font.pixelSize:Typography.body;color:Colors.warning;wrapMode:Text.Wrap}
                    ListView{id:microphonesList;Layout.fillWidth:true;implicitHeight:contentHeight;interactive:false;spacing:10;model:root.status.microphones||[];delegate:Column{required property var modelData;width:microphonesList.width;spacing:5
                        Text{width:parent.width;text:modelData.name;font.pixelSize:Typography.body;color:Colors.text;wrapMode:Text.Wrap}
                        Text{width:parent.width;text:modelData.state;font.pixelSize:Typography.body;color:modelData.accessible&&!modelData.muted?Colors.success:Colors.warning;wrapMode:Text.Wrap}
                        Text{width:parent.width;text:modelData.detail;font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.WrapAnywhere}
                    }}
                    Text{visible:!(root.status.microphones||[]).length;Layout.fillWidth:true;text:root.status.audio_service===false?"音频服务查询失败，输入设备状态待确认":"未检测到麦克风";font.pixelSize:Typography.body;color:Colors.warning}
                    Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                    Text{text:"扬声器 / 音频输出";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    ListView{id:speakersList;Layout.fillWidth:true;implicitHeight:contentHeight;interactive:false;spacing:10;model:root.status.speakers||[];delegate:Column{required property var modelData;width:speakersList.width;spacing:5
                        Text{width:parent.width;text:modelData.name+(modelData.default?" · 默认输出":"");font.pixelSize:Typography.body;color:Colors.text;wrapMode:Text.Wrap}
                        Text{width:parent.width;text:modelData.state;font.pixelSize:Typography.body;color:modelData.muted?Colors.warning:Colors.textSecondary;wrapMode:Text.Wrap}
                        Text{width:parent.width;text:modelData.detail;font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}
                    }}
                    Text{visible:!(root.status.speakers||[]).length;Layout.fillWidth:true;text:root.status.audio_service===false?"音频服务查询失败，输出设备状态待确认":"未检测到音频输出设备";font.pixelSize:Typography.body;color:Colors.warning}
                    Rectangle{Layout.fillWidth:true;height:1;color:Colors.border}
                    Text{text:"网络接口";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    ListView{id:networkList;Layout.fillWidth:true;implicitHeight:contentHeight;interactive:false;spacing:8;model:root.status.networks||[];delegate:Text{required property var modelData;width:networkList.width;text:modelData.name+" · "+(modelData.state==="up"?"已连接":modelData.state==="down"?"未连接":modelData.state)+" · "+(modelData.address||"无 IPv4 地址");font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}}
                }
            }
        }
        RuntimeStatusPanel{Layout.fillWidth:true;Layout.fillHeight:true}
    }
}
