// Shown instead of the app when it cannot start (work folder, lock,
// database): what is wrong, in plain words, and a Quit button. Also the
// last window of an in-app uninstall (src/app/main.cpp): what was removed
// and kept.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    required property string message
    property string heading: "The app cannot start"
    width: 640
    height: 360
    visible: true
    title: Qt.application.displayName
    color: Theme.ground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: Theme.gap
        Text {
            text: window.heading
            font.family: Theme.sans
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextEdit {
                width: scroll.availableWidth
                text: window.message
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.secondaryText
            }
        }
        AppButton {
            Layout.alignment: Qt.AlignRight
            text: "Quit"
            primary: true
            onClicked: Qt.quit()
        }
    }
}
