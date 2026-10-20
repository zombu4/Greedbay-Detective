// A small label with words (color is never the only signal): "warn"
// (amber badge), "info" (blue badge), "danger" (red badge, LIKELY BROKEN) or
// "accent" (THIS LISTING) or "neutral" (formats: BUY IT NOW, AUCTION - 3 bids).
import QtQuick

Rectangle {
    id: pill
    property string text
    property string kind: "info"
    implicitWidth: label.implicitWidth + 20
    implicitHeight: label.implicitHeight + 8
    radius: 999
    color: kind === "warn" ? Theme.warnBadgeBackground : kind === "danger" ? Theme.dangerBadgeBackground
         : kind === "accent" ? Theme.accent : kind === "neutral" ? Theme.raised : Theme.infoBadgeBackground
    border.color: kind === "neutral" ? Theme.inputBorder : "transparent"
    Text {
        id: label
        anchors.centerIn: parent
        text: pill.text
        font.family: Theme.sans
        font.pixelSize: 12
        font.weight: Font.DemiBold
        color: pill.kind === "warn" ? Theme.warnBadgeText : pill.kind === "danger" ? Theme.dangerBadgeText
             : pill.kind === "accent" ? Theme.accentText : pill.kind === "neutral" ? Theme.secondaryText
             : Theme.infoBadgeText
    }
}
