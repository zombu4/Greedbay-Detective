// A flag card (ui-spec 2): a word label (SHIPPING, CHEAPER RIGHT NOW) and
// its lines; "warn" or "info" colors, never color alone.
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
    fill: kind === "warn" ? Theme.warnBackground : Theme.infoBackground
    line: kind === "warn" ? Theme.warnBorder : Theme.infoBorder

    ColumnLayout {
        spacing: 6
        Text {
            text: flag.label
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: flag.kind === "warn" ? Theme.warnLabel : Theme.infoLabel
        }
        Repeater {
            model: flag.lines
            Text {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                wrapMode: Text.WordWrap
                font.family: Theme.sans
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
