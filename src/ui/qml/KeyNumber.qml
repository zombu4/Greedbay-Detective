// One key number: a muted label above a mono value (ui-spec: 24 mono).
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property string label
    property string value
    spacing: 4
    Text {
        text: parent.label
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Text {
        text: parent.value
        font.family: Theme.mono
        font.pixelSize: Theme.keyNumberSize
        font.weight: Font.Medium
        color: Theme.text
    }
}
