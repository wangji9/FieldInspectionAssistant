import QtQuick
import QtQuick.Controls
import "../theme"

VoiceInputButton {
    text:""; width:42; height:38; icon:"mic"; variant:"secondary"
    Accessible.role:Accessible.Button
    Accessible.name:"麦克风语音输入"
}
