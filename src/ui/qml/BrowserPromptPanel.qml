// eBay needs the user (sign in, ship-to, a check page): the browser window
// is in front; the user answers it there, then presses Continue. The app
// never fills in, solves or skips anything.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property string message
    property string browserName
    signal continueRequested()
    signal cancelRequested()

    spacing: 12

    Text {
        text: "eBay needs you in the browser window"
        font.family: Theme.sans
        font.pixelSize: Theme.sectionSize
        font.weight: Font.DemiBold
        color: Theme.warnLabel
    }
    Text {
        Layout.fillWidth: true
        text: panel.message
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        font.family: Theme.sans
        font.pixelSize: Theme.bodySize
        color: Theme.text
    }
    Text {
        Layout.fillWidth: true
        text: "Answer it in the " + (panel.browserName.length > 0 ? panel.browserName : "browser")
              + " window, then press Continue here."
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12
        AppButton {
            text: "Cancel"
            Accessible.name: "Cancel the lookup"
            onClicked: panel.cancelRequested()
        }
        AppButton {
            text: "Continue"
            primary: true
            onClicked: panel.continueRequested()
        }
    }
}
