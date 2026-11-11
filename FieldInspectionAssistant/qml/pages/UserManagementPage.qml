import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id:root;objectName:"userManagementPage";color:Colors.background
    property var rows:[]
    property string query:""
    property string message:""
    property var selected:({})
    property var roleKeys:["inspector","supervisor","admin"]
    property var roleNames:["巡检员","主管","系统管理员"]
    function refresh(){rows=App.session.users()}
    function edit(row){selected=row||{};username.text=selected.name||"";password.text="";roleChoice.currentIndex=Math.max(0,roleKeys.indexOf(selected.role||"inspector"));activeBox.checked=selected.active===undefined?true:!!selected.active;message="";editor.visible=true}
    function save(){var r=App.session.saveUser(selected.id||"",username.text,roleKeys[roleChoice.currentIndex],activeBox.checked,password.text);if(r.ok){editor.visible=false;password.text="";message="用户已保存";refresh()}else message=r.error}
    onVisibleChanged:if(visible)refresh()
    Connections{target:App.session;function onUsersChanged(){root.refresh()}}
    ColumnLayout {
        anchors.fill:parent;anchors.margins:14;spacing:12
        RowLayout{Layout.fillWidth:true
            Text{text:"用户管理";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
            Item{Layout.fillWidth:true}
            Text{text:App.session.userName+" · "+App.session.roleText();font.pixelSize:Typography.body;color:Colors.textSecondary}
            TouchButton{text:"退出登录";autoWidth:true;height:32;variant:"ghost";onClicked:App.session.logout()}
            TouchButton{objectName:"newUserButton";text:"新增用户";autoWidth:true;height:32;variant:"primary";enabled:App.session.role==="admin";onClicked:root.edit({})}
        }
        FieldInput{Layout.fillWidth:true;height:36;placeholder:"搜索用户名或角色";onTextEdited:function(value){root.query=value}}
        Text{Layout.fillWidth:true;visible:root.message.length>0;text:root.message;color:Colors.textSecondary;wrapMode:Text.Wrap;font.pixelSize:Typography.body}
        Text{visible:App.session.role!=="admin";text:"仅管理员可新增、编辑、停用和删除用户。";font.pixelSize:Typography.body;color:Colors.textSecondary}
        ScrollView {
            id:userScroll;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:userScroll.availableWidth;spacing:8
                Repeater{model:root.rows
                    Rectangle {
                        property var entry:modelData
                        visible:!root.query||entry.name.indexOf(root.query)>=0||root.roleNames[Math.max(0,root.roleKeys.indexOf(entry.role))].indexOf(root.query)>=0
                        Layout.fillWidth:true;Layout.preferredHeight:78;color:Colors.panel;radius:12;border.color:Colors.border
                        RowLayout{anchors.fill:parent;anchors.margins:12;spacing:12
                            Rectangle{width:38;height:38;radius:12;color:Colors.infoDark;Text{anchors.centerIn:parent;text:entry.name.slice(0,1);font.pixelSize:18;color:Colors.normal}}
                            ColumnLayout{Layout.fillWidth:true;spacing:4
                                Text{Layout.fillWidth:true;text:entry.name+(entry.name===App.session.userName?"（当前）":"");font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text;elide:Text.ElideRight}
                                Text{Layout.fillWidth:true;text:root.roleNames[Math.max(0,root.roleKeys.indexOf(entry.role))]+" · "+(entry.configured?"已设置密码":"待设置密码");font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                            }
                            StatusBadge{text:entry.active?"启用":"停用";status:entry.active?"normal":"pending"}
                            TouchButton{text:"编辑";width:60;height:32;variant:"ghost";enabled:App.session.role==="admin";onClicked:root.edit(entry)}
                            TouchButton{text:"删除";width:60;height:32;variant:"ghost";enabled:App.session.role==="admin"&&entry.name!==App.session.userName;onClicked:{root.selected=entry;confirm.openDialog()}}
                        }
                    }
                }
            }
        }
    }
    ConfirmDialog{id:confirm;title:"删除用户";message:"确定删除用户“"+(root.selected.name||"")+"”？关联业务记录的账户应停用。";onAccepted:{var r=App.session.removeUser(root.selected.id);root.message=r.ok?"用户已删除":r.error;root.refresh()}}
    Rectangle {
        id:editor;objectName:"userEditor";anchors.fill:parent;z:50;visible:false;color:"#880D152B"
        MouseArea{anchors.fill:parent}
        Rectangle {
            anchors.centerIn:parent;width:Math.min(480,parent.width-24);height:Math.min(500,parent.height-24);radius:16;color:Colors.panel
            ColumnLayout{anchors.fill:parent;anchors.margins:20;spacing:12
                Text{text:root.selected.id?"编辑用户":"新增用户";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
                ScrollView{id:editScroll;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                    ColumnLayout{width:editScroll.availableWidth;spacing:14
                        Text{text:"用户名";font.pixelSize:Typography.body;color:Colors.text}
                        FieldInput{id:username;Layout.fillWidth:true;height:38;maximumLength:64}
                        Text{text:root.selected.id?"新密码（留空保持原密码）":"密码（至少 8 个字符）";font.pixelSize:Typography.body;color:Colors.text}
                        FieldInput{id:password;Layout.fillWidth:true;height:38;maximumLength:256;echoMode:TextInput.Password;inputMethodHints:Qt.ImhSensitiveData|Qt.ImhNoPredictiveText}
                        AppComboBox{id:roleChoice;Layout.fillWidth:true;height:38;model:root.roleNames}
                        AppCheckBox{id:activeBox;text:"启用账户";font.pixelSize:Typography.body}
                        Text{Layout.fillWidth:true;text:root.message;visible:text.length>0;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
                    }
                }
                RowLayout{Layout.fillWidth:true;Item{Layout.fillWidth:true}TouchButton{text:"取消";autoWidth:true;height:34;variant:"ghost";onClicked:{editor.visible=false;password.text=""}}TouchButton{objectName:"saveUserButton";text:"保存";autoWidth:true;height:34;variant:"primary";onClicked:root.save()}}
            }
        }
    }
}
