// Progress while a lookup runs: the steps (done / now / to do, in words,
// not only color), the eBay pages this lookup and today, the last thing the
// app did, and Cancel.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property var steps: []
    property int step: 0
    property int pages: 0
    property string pagesToday
    property string lastLine
    property string browserName
    signal cancelRequested()

    spacing: 12

    Text {
        text: "Checking the listing"
        font.family: Theme.sans
        font.pixelSize: Theme.sectionSize
        font.weight: Font.DemiBold
        color: Theme.text
    }
    Text {
        Layout.fillWidth: true
        text: "The app reads eBay's pages in " + (panel.browserName.length > 0 ? panel.browserName : "your browser")
              + " (its own profile, window minimized), one page at a time with a pause between pages."
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Repeater {
        model: panel.steps
        RowLayout {
            id: stepRow
            required property string modelData
            required property int index
            spacing: 12
            Text {
                Layout.preferredWidth: 52
                text: stepRow.index < panel.step ? "done" : stepRow.index === panel.step ? "now" : ""
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                font.weight: Font.DemiBold
                color: stepRow.index === panel.step ? Theme.accent : Theme.mutedText
            }
            Text {
                text: stepRow.modelData
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                font.weight: stepRow.index === panel.step ? Font.DemiBold : Font.Normal
                color: stepRow.index <= panel.step ? Theme.text : Theme.mutedText
            }
        }
    }
    Text {
        text: "eBay pages this lookup: " + panel.pages + "  ·  " + panel.pagesToday
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.secondaryText
    }
    Text {
        Layout.fillWidth: true
        text: panel.lastLine
        elide: Text.ElideRight
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    AppButton {
        Layout.alignment: Qt.AlignRight
        text: "Cancel"
        Accessible.name: "Cancel the lookup"
        onClicked: panel.cancelRequested()
    }
}
