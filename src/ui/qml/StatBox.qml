// A bordered box with its own heading (ui-spec 2): the content (tiles, a
// plain line), then its notes as small muted footnotes at the bottom.
import QtQuick
import QtQuick.Layouts

Card {
    id: box
    property string heading
    property var footnotes: []
    default property alias content: body.data

    Layout.fillWidth: true
    Accessible.role: Accessible.Grouping
    Accessible.name: heading

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Text {
            Layout.fillWidth: true
            text: box.heading
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.sectionSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 12
        }
        Repeater {
            model: box.footnotes
            Text {
                required property string modelData
                Layout.fillWidth: true
                visible: modelData.length > 0
                text: modelData
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
            }
        }
    }
}
