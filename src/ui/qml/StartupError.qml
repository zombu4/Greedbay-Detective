// Shown instead of the app when it cannot start (work folder, lock,
// database): what is wrong, in plain words, and a Quit button.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    required property string message
    width: 640
    height: 320
    visible: true
    title: Qt.application.displayName
    color: Theme.ground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: Theme.gap
        Text {
            text: "The app cannot start"
            font.family: Theme.sans
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: window.message
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.family: Theme.sans
            font.pixelSize: Theme.bodySize
            color: Theme.secondaryText
        }
        AppButton {
            Layout.alignment: Qt.AlignRight
            text: "Quit"
            primary: true
            onClicked: Qt.quit()
        }
    }
}
