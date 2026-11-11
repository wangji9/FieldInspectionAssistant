pragma Singleton
import QtQuick

QtObject {
    readonly property string uiFamily: FieldOpsFontFamily
    readonly property string cjkFamily: uiFamily
    readonly property string numericFamily: uiFamily
    readonly property string monoFamily: uiFamily
    readonly property int title: 14
    readonly property int h2: 13
    readonly property int body: 12
    readonly property int small: body
    readonly property int button: body
    readonly property int table: body
    readonly property int device: h2
    readonly property int value: 16
    readonly property int largeValue: 20
    readonly property int code: body
}
