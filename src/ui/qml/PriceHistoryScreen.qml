// Price history (ui-spec 3) is built in the next phase; this screen says so.
import QtQuick
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 8
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Price history"
            font.family: Theme.sans
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Coming in the next phase: sold prices over time for the items you checked."
            font.family: Theme.sans
            font.pixelSize: Theme.bodySize
            color: Theme.mutedText
        }
    }
}
