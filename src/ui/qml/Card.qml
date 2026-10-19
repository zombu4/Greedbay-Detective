// A rounded surface card (ui-spec: radius 12-14, padding 20-24).
import QtQuick
import QtQuick.Controls.Basic

Pane {
    id: card
    property color fill: Theme.surface
    property color line: Theme.border
    padding: Theme.cardPadding
    background: Rectangle {
        color: card.fill
        radius: Theme.cardRadius
        border.color: card.line
        border.width: 1
    }
}
