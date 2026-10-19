// A flag card (ui-spec 2): a word label (SHIPPING, CHEAPER RIGHT NOW, LIKELY
// BROKEN) and its lines; "warn", "info" or "danger" colors, never color alone.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Card {
    id: flag
    property string kind: "warn"
    property string label
    property var lines: []
    property string linkText
    signal linkActivated()
    fill: kind === "warn" ? Theme.warnBackground : kind === "danger" ? Theme.dangerBackground : Theme.infoBackground
    line: kind === "warn" ? Theme.warnBorder : kind === "danger" ? Theme.dangerBorder : Theme.infoBorder

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Text {
            text: flag.label
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: flag.kind === "warn" ? Theme.warnLabel : flag.kind === "danger" ? Theme.dangerLabel : Theme.infoLabel
        }
        Repeater {
            model: flag.lines
            Text {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                wrapMode: Text.WordWrap
                // Amounts and ratios: mono (ui-spec "Look").
                font.family: Theme.mono
                font.pixelSize: Theme.bodySize
                color: Theme.text
            }
        }
        LinkText {
            visible: flag.linkText.length > 0
            text: flag.linkText
            onActivated: flag.linkActivated()
        }
    }
}
