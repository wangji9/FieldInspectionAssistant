import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root;objectName:"loginPage";color:Colors.background
    property bool working:false
    function submit(){
        if(working)return
        if(App.session.setupRequired&&passwordField.text!==confirmField.text){localMessage="两次输入的密码不一致";return}
        working=true;localMessage=""
        Qt.callLater(function(){var ok=App.session.setupRequired?App.session.setupAdministrator(nameField.text,passwordField.text,remember.checked):App.session.authenticate(nameField.text,passwordField.text,remember.checked);working=false;if(ok){passwordField.text="";confirmField.text="";App.nav.push("dashboard")}})
    }
    property string localMessage:""
        GroupBox {
            title: "现场巡检助手"
            anchors.centerIn: parent
            width: Math.min(root.width - 24, 400)
            height: Math.min(root.height - 24, App.session.setupRequired ? 412 : 342)
            ScrollView {
                id:loginScroll;anchors.fill:parent;anchors.margins:8;contentWidth:availableWidth;clip:true
                ColumnLayout {
                    width:loginScroll.availableWidth;spacing:10
                    Text{text:App.session.setupRequired?"设置管理员":"用户登录";font.pixelSize:Typography.title;color:Colors.text}
                    Text{visible:App.session.setupRequired;Layout.fillWidth:true;text:"首次使用请创建管理员账户，密码至少 8 个字符。";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                    Text{text:"用户名";font.pixelSize:Typography.body;color:Colors.text}
                    FieldInput{id:nameField;objectName:"loginUsername";Layout.fillWidth:true;height:42;maximumLength:64;placeholder:"输入用户名"}
                    Text{text:"密码";font.pixelSize:Typography.body;color:Colors.text}
                    FieldInput{id:passwordField;objectName:"loginPassword";Layout.fillWidth:true;height:42;maximumLength:256;echoMode:TextInput.Password;inputMethodHints:Qt.ImhSensitiveData|Qt.ImhNoPredictiveText;placeholder:"输入密码"}
                    Text{visible:App.session.setupRequired;text:"确认密码";font.pixelSize:Typography.body;color:Colors.text}
                    FieldInput{id:confirmField;visible:App.session.setupRequired;Layout.fillWidth:true;height:42;maximumLength:256;echoMode:TextInput.Password;inputMethodHints:Qt.ImhSensitiveData|Qt.ImhNoPredictiveText;placeholder:"再次输入密码"}
                    AppCheckBox { id: remember; text: "在本机保持登录"; checked: true; enabled: !root.working }
                    Text{Layout.fillWidth:true;visible:text.length>0;text:root.localMessage||App.session.message;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.danger}
                    TouchButton{objectName:"loginSubmit";Layout.fillWidth:true;text:root.working?"正在验证…":App.session.setupRequired?"创建并登录":"登录系统";variant:"primary";enabled:!root.working&&nameField.text.trim().length>0&&passwordField.text.length>0;onClicked:root.submit()}
                }
            }
        }
}
