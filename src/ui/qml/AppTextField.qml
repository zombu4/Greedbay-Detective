// A text field in the mockups' look; `mono` for links, numbers and ids.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: field
    property bool mono: false
    implicitHeight: Theme.target
    leftPadding: 12
    rightPadding: 12
    color: Theme.text
    placeholderTextColor: Theme.mutedText
    selectionColor: Theme.band
    selectedTextColor: Theme.text
    font.family: mono ? Theme.mono : Theme.sans
    font.pixelSize: Theme.bodySize
    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.raised
        border.color: field.activeFocus ? Theme.accent : Theme.inputBorder
        border.width: 1
    }
}
