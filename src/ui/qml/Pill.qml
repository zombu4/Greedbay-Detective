// A small label with words (color is never the only signal): "warn"
// (amber badge), "info" (blue badge) or "accent" (THIS LISTING).
import QtQuick

Rectangle {
    id: pill
    property string text
    property string kind: "info"
    implicitWidth: label.implicitWidth + 20
    implicitHeight: label.implicitHeight + 8
    radius: 999
    color: kind === "warn" ? Theme.warnBadgeBackground : kind === "accent" ? Theme.accent : Theme.infoBadgeBackground
    Text {
        id: label
        anchors.centerIn: parent
        text: pill.text
        font.family: Theme.sans
        font.pixelSize: 12
        font.weight: Font.DemiBold
        color: pill.kind === "warn" ? Theme.warnBadgeText : pill.kind === "accent" ? Theme.accentText : Theme.infoBadgeText
    }
}
