// Text that acts as a link (keyboard: focus + Enter or Space).
import QtQuick

Text {
    id: link
    signal activated()
    color: Theme.link
    font.family: Theme.sans
    font.pixelSize: Theme.bodySize
    font.underline: area.containsMouse || activeFocus
    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.onPressAction: link.activated()
    Keys.onReturnPressed: link.activated()
    Keys.onSpacePressed: link.activated()
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: link.activated()
    }
}
