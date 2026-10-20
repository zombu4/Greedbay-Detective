// One number in a tile of its own (ui-spec 2): a small label on top, the
// big mono number under it, an optional muted line under that (confidence,
// "not a final price"), and an optional link ("View").
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: tile
    property string label
    property string value
    property string detail
    property string linkText
    signal linkActivated()

    Layout.fillWidth: true
    Layout.preferredWidth: 1
    Layout.fillHeight: true
    implicitHeight: column.implicitHeight + 2 * Theme.gap
    radius: 10
    color: Theme.raised
    border.color: Theme.border
    border.width: 1
    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value + (detail.length > 0 ? ", " + detail : "")

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.gap
        spacing: 4
        Text {
            Layout.fillWidth: true
            text: tile.label
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
        }
        RowLayout {
            spacing: Theme.unit
            Text {
                text: tile.value
                font.family: Theme.mono
                font.pixelSize: Theme.keyNumberSize
                font.weight: Font.Medium
                color: Theme.text
            }
            LinkText {
                visible: tile.linkText.length > 0
                text: tile.linkText
                font.pixelSize: Theme.labelSize
                Accessible.name: tile.linkText + " " + tile.label
                onActivated: tile.linkActivated()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: tile.detail
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
        }
    }
}
