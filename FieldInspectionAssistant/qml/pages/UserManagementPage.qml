import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Item {
    id: root
    objectName: "userManagementPage"
    property var rows:[]
    property string query:""
    property string message:""
    property var selected:({})
    property var roleKeys:["inspector","supervisor","admin"]
    property var roleNames:["巡检员","主管","系统管理员"]
    function refresh() {
        rows = App.session.users()
        selected = rows.find(row => row.id === selected.id) || ({})
    }
    function edit(row){selected=row||{};username.text=selected.name||"";password.text="";roleChoice.currentIndex=Math.max(0,roleKeys.indexOf(selected.role||"inspector"));activeBox.checked=selected.active===undefined?true:!!selected.active;message="";editor.visible=true}
    function save(){var r=App.session.saveUser(selected.id||"",username.text,roleKeys[roleChoice.currentIndex],activeBox.checked,password.text);if(r.ok){editor.visible=false;password.text="";message="用户已保存";refresh()}else message=r.error}
    onVisibleChanged:if(visible)refresh()
    Connections{target:App.session;function onUsersChanged(){root.refresh()}}
    readonly property var visibleRows: rows.map(function(row) {
        const role = root.roleNames[Math.max(0, root.roleKeys.indexOf(row.role))]
        return Object.assign({}, row, {roleLabel:role, displayName:row.name + (row.name === App.session.userName ? "（当前）" : ""),
                                      state:row.active ? "启用" : "停用", credential:row.configured ? "已设置密码" : "待设置密码"})
    }).filter(row => !root.query || row.name.toLowerCase().includes(root.query.toLowerCase()) || row.roleLabel.includes(root.query))

    SettingsPanel {
        anchors.fill: parent
        title: "账号与角色"

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flow {
                id: userTools
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: implicitHeight
                spacing: 6
                FieldInput {
                    width: Math.min(220, Math.max(130, parent.width - 300))
                    height: 30
                    placeholder: "搜索用户名或角色"
                    text: root.query
                    onTextEdited: function(value) { root.query = value }
                }
                TouchButton {
                    objectName: "newUserButton"
                    text: "新增用户"; autoWidth: true; variant: "primary"
                    enabled: App.session.role === "admin"
                    onClicked: root.edit({})
                }
                TouchButton {
                    text: "编辑"; autoWidth: true
                    enabled: App.session.role === "admin" && !!root.selected.id
                    onClicked: root.edit(root.selected)
                }
                TouchButton {
                    text: "删除"; autoWidth: true
                    enabled: App.session.role === "admin" && !!root.selected.id && root.selected.name !== App.session.userName
                    onClicked: confirm.openDialog()
                }
                TouchButton { text: "退出登录"; autoWidth: true; onClicked: App.session.logout() }
            }
            Label {
                id: userMessage
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: userTools.bottom
                anchors.topMargin: 6
                visible: !!text
                text: root.message || (App.session.role === "admin" ? "" : "仅管理员可管理用户。")
                color: Colors.textSecondary
                wrapMode: Text.Wrap
            }
            DataTable {
                objectName: "settingsUserTable"
                spreadsheet: true
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: userMessage.visible ? userMessage.bottom : userTools.bottom
                anchors.topMargin: 6
                anchors.bottom: parent.bottom
                rowHeight: 30
                items: root.visibleRows
                selectedIndex: items.findIndex(row => row.id === root.selected.id)
                emptyText: root.query ? "没有匹配的用户" : "暂无用户"
                columns: [{key:"displayName",title:"用户名",width:0}, {key:"roleLabel",title:"角色",width:115},
                          {key:"state",title:"账户状态",width:85}, {key:"credential",title:"登录凭据",width:110}]
                onRowClicked: function(index, row) { root.selected = row }
                onRowDoubleClicked: function(index, row) { if (App.session.role === "admin") root.edit(row) }
            }
        }
    }
    ConfirmDialog{id:confirm;title:"删除用户";message:"确定删除用户“"+(root.selected.name||"")+"”？关联业务记录的账户应停用。";onAccepted:{var r=App.session.removeUser(root.selected.id);root.message=r.ok?"用户已删除":r.error;root.refresh()}}
    Rectangle {
        id:editor;objectName:"userEditor";anchors.fill:parent;z:50;visible:false;color:"#880D152B"
        MouseArea{anchors.fill:parent}
        Rectangle {
            anchors.centerIn:parent;width:Math.min(480,parent.width-24);height:Math.min(500,parent.height-24);color:Colors.panel;border.color:Colors.border
            ColumnLayout{anchors.fill:parent;anchors.margins:10;spacing:8
                Text{text:root.selected.id?"编辑用户":"新增用户";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
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
