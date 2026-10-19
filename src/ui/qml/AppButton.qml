// A button: "primary" is amber with dark text (ui-spec accent), otherwise a
// raised button with a border. At least 44 px high.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: button
    property bool primary: false
    implicitHeight: Theme.target
    leftPadding: 18
    rightPadding: 18
    font.family: Theme.sans
    font.pixelSize: Theme.bodySize
    font.weight: Font.DemiBold
    opacity: enabled ? 1.0 : 0.45
    contentItem: Text {
        text: button.text
        font: button.font
        color: button.primary ? Theme.accentText : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: button.primary ? (button.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
                              : (button.down ? Theme.border : Theme.raised)
        border.color: button.primary ? Theme.accent : (button.visualFocus ? Theme.accent : Theme.inputBorder)
        border.width: 1
    }
}
