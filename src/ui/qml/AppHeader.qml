// The header of every screen (ui-spec "Header"): the app's name (click =
// Home) and tagline on the left; Lookups | Price history | Settings on the
// right, the current one on the raised background.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: header
    property string appName
    property string tagline
    property int current: 0
    signal homeRequested()
    signal navigate(int index)

    implicitHeight: 76
    color: Theme.ground
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        spacing: Theme.gap

        Column {
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Text {
                text: header.appName
                font.family: Theme.sans
                font.pixelSize: Theme.titleSize
                font.weight: Font.DemiBold
                color: Theme.text
                Accessible.role: Accessible.Button
                Accessible.name: header.appName + ", home"
                Accessible.onPressAction: header.homeRequested()
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: header.homeRequested()
                }
            }
            Text {
                text: header.tagline
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
            }
        }

        Item { Layout.fillWidth: true }

        Repeater {
            model: ["Lookups", "Price history", "Settings"]
            Button {
                id: nav
                required property string modelData
                required property int index
                text: modelData
                implicitHeight: Theme.target
                leftPadding: 16
                rightPadding: 16
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                font.weight: Font.Medium
                Accessible.name: modelData
                contentItem: Text {
                    text: nav.text
                    font: nav.font
                    color: header.current === nav.index ? Theme.text : Theme.secondaryText
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: header.current === nav.index ? Theme.raised : "transparent"
                    border.color: nav.visualFocus ? Theme.accent : "transparent"
                }
                onClicked: header.navigate(index)
            }
        }
    }
}
