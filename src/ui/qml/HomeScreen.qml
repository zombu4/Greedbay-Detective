// Home (ui-spec 1): the paste field and Check button, then the recent
// lookups as a 2-column grid of cards with the pages used today.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: home
    required property Backend backend
    property string error
    // Let the user check the searches and depths before they run.
    property alias reviewFirst: review.checked

    function setInput(text) {
        input.text = text
        error = ""
    }
    function showError(message) {
        error = message
    }
    function check() {
        error = ""
        if (!backend.check(input.text, reviewFirst))
            error = backend.session.errorText
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: Theme.gap

        Text {
            text: "Paste an eBay listing link"
            font.family: Theme.sans
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            AppTextField {
                id: input
                Layout.fillWidth: true
                mono: true
                placeholderText: "https://www.ebay.com/itm/..."
                Accessible.name: "eBay listing link"
                onAccepted: home.check()
            }
            AppButton {
                text: "Check"
                primary: true
                enabled: !home.backend.session.running && input.text.trim().length > 0
                Accessible.name: "Check this listing"
                onClicked: home.check()
            }
        }
        CheckBox {
            id: review
            text: "Let me check the searches and how deep they go before they run"
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize + 1
        }
        Text {
            Layout.fillWidth: true
            text: "Paste or drop a listing link anywhere in this window. The app opens your browser in its own profile, "
                  + "reads the listing and what the same item sold for, and keeps every lookup here."
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: home.error.length > 0 ? home.error : home.backend.notice
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.bodySize
            color: Theme.warnLabel
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.unit
            Text {
                text: "Recent lookups"
                font.family: Theme.sans
                font.pixelSize: Theme.sectionSize
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Text {
                text: home.backend.pagesTodayText
                font.family: Theme.mono
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
            }
        }

        Text {
            visible: home.backend.recent.count === 0
            text: "No lookups yet. Paste a listing link above to start."
            font.family: Theme.sans
            font.pixelSize: Theme.bodySize
            color: Theme.mutedText
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: home.backend.recent
            cellWidth: Math.floor(width / 2)
            cellHeight: 150
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: cell
                required property var lookupId
                required property string name
                required property string checked
                required property string flagText
                required property string flagKind
                required property string listingTotal
                required property string soldMedian
                required property string broken
                width: grid.cellWidth
                height: grid.cellHeight
                Card {
                    id: card
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    anchors.bottomMargin: 12
                    line: area.containsMouse || activeFocus ? Theme.inputBorder : Theme.border
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: cell.name + ", " + cell.checked
                    Accessible.onPressAction: home.backend.openSaved(cell.lookupId)
                    Keys.onReturnPressed: home.backend.openSaved(cell.lookupId)
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: cell.name
                                elide: Text.ElideRight
                                font.family: Theme.sans
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Pill {
                                visible: cell.broken.length > 0
                                text: cell.broken
                                kind: "danger"
                            }
                            Pill {
                                visible: cell.flagText.length > 0
                                text: cell.flagText
                                kind: cell.flagKind
                            }
                        }
                        Text {
                            text: cell.checked
                            font.family: Theme.mono
                            font.pixelSize: Theme.labelSize
                            color: Theme.mutedText
                        }
                        Item { Layout.fillHeight: true }
                        RowLayout {
                            spacing: 32
                            KeyNumber {
                                label: "Listing total"
                                value: cell.listingTotal
                            }
                            KeyNumber {
                                label: "Sold median"
                                value: cell.soldMedian
                            }
                        }
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: home.backend.openSaved(cell.lookupId)
                    }
                }
            }
        }
    }

    // A problem after the lookup started (the browser stopped, the save
    // failed) is shown here; the overlay is gone by then.
    Connections {
        target: home.backend
        function onSessionFailed(message) { home.error = message }
    }
}
