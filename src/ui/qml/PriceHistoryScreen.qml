// Price history (ui-spec 3): pills to pick a checked item; "<item> - sold
// prices over time"; the window buttons 90 days | 1 year | 3 years and the
// amber "Re-check now"; the chart with its legend; "Your checks of this
// item" (Checked | Listing | Its total | Median sold then | Flags). A check's
// time opens that lookup on the Results screen; its item number opens the
// listing in the system browser.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: screen
    required property Backend backend
    readonly property HistoryController history: backend.history
    property string error

    onVisibleChanged: {
        if (visible) {
            error = ""
            history.refresh()
        }
    }

    component LegendDot: Rectangle {
        width: 10
        height: 10
        radius: 5
    }
    component LegendText: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    component HeaderText: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        font.weight: Font.Medium
        color: Theme.mutedText
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            x: 28
            y: 28
            width: parent.width - 56
            spacing: Theme.gap

            Text {
                Layout.fillWidth: true
                visible: screen.history.items.length === 0 || screen.history.message.length > 0
                text: screen.history.message
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.mutedText
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: screen.history.items
                    Rectangle {
                        id: pick
                        required property var modelData
                        readonly property bool current: modelData.key === screen.history.currentKey
                        width: pickText.implicitWidth + 28
                        height: 36
                        radius: 999
                        color: current ? Theme.accent : Theme.raised
                        border.color: pickArea.containsMouse || activeFocus ? Theme.accent : Theme.inputBorder
                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name
                        Accessible.checked: current
                        Accessible.onPressAction: screen.history.select(modelData.key)
                        Keys.onReturnPressed: screen.history.select(modelData.key)
                        Keys.onSpacePressed: screen.history.select(modelData.key)
                        Text {
                            id: pickText
                            anchors.centerIn: parent
                            text: pick.modelData.name
                            textFormat: Text.PlainText
                            font.family: Theme.sans
                            font.pixelSize: Theme.labelSize + 1
                            font.weight: Font.DemiBold
                            color: pick.current ? Theme.accentText : Theme.text
                        }
                        MouseArea {
                            id: pickArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: screen.history.select(pick.modelData.key)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: screen.history.heading.length > 0
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: screen.history.heading
                    elide: Text.ElideRight
                    font.family: Theme.sans
                    font.pixelSize: Theme.headingSize
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Repeater {
                    model: [{days: 90, text: "90 days"}, {days: 365, text: "1 year"}, {days: 1095, text: "3 years"}]
                    AppButton {
                        id: windowButton
                        required property var modelData
                        readonly property bool current: screen.history.windowDays === modelData.days
                        text: modelData.text
                        Accessible.name: "Show the last " + modelData.text
                        Accessible.checked: current
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: windowButton.current ? Theme.border : Theme.raised
                            border.color: windowButton.current || windowButton.visualFocus ? Theme.accent : Theme.inputBorder
                        }
                        onClicked: screen.history.setWindow(modelData.days)
                    }
                }
                AppButton {
                    text: "Re-check now"
                    primary: true
                    enabled: screen.history.canRecheck && !screen.backend.session.running
                    Accessible.name: "Check this item again now"
                    onClicked: {
                        screen.error = ""
                        screen.history.recheck()
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: screen.error.length > 0
                text: screen.error
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.warnLabel
            }

            // The window's numbers as tiles, as on the Results screen
            // (ui-spec 2); without a sale, the summary says why.
            StatBox {
                objectName: "historyNumbers"
                visible: screen.history.medianText.length > 0
                heading: screen.history.summary
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.unit
                    NumberTile { label: "Median sold"; value: screen.history.medianText }
                    NumberTile { label: "Middle half"; value: screen.history.middleText }
                    NumberTile { label: "Sales counted"; value: screen.history.salesText }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0 && screen.history.medianText.length === 0
                text: screen.history.summary
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
            }

            Card {
                Layout.fillWidth: true
                visible: screen.history.heading.length > 0
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    PriceChart {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 320
                        sales: screen.history.sales
                        checks: screen.history.checks
                        minTime: screen.history.minTime
                        maxTime: screen.history.maxTime
                        minCents: screen.history.minCents
                        maxCents: screen.history.maxCents
                        bandLow: screen.history.bandLow
                        bandHigh: screen.history.bandHigh
                        median: screen.history.median
                        medianLabel: screen.history.medianLabel
                        startLabel: screen.history.startLabel
                        endLabel: screen.history.endLabel
                        lowLabel: screen.history.lowLabel
                        highLabel: screen.history.highLabel
                    }
                    // The legend: every mark with its words.
                    Flow {
                        Layout.fillWidth: true
                        spacing: 20
                        Row {
                            spacing: 6
                            LegendDot { anchors.verticalCenter: parent.verticalCenter; color: Theme.secondaryText }
                            LegendText { text: "a sale (total incl. shipping)" }
                        }
                        Row {
                            spacing: 6
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 18; height: 10; color: Theme.band }
                            LegendText { text: "middle half of the sales" }
                        }
                        Row {
                            spacing: 6
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Repeater {
                                    model: 3
                                    Rectangle { width: 5; height: 2; color: Theme.text }
                                }
                            }
                            LegendText { text: "median" }
                        }
                        Row {
                            spacing: 6
                            LegendDot { anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                            LegendText { text: "a listing you checked (its total)" }
                        }
                    }
                }
            }

            Text {
                visible: screen.history.heading.length > 0
                Layout.topMargin: Theme.unit
                text: "Your checks of this item"
                font.family: Theme.sans
                font.pixelSize: Theme.sectionSize
                font.weight: Font.DemiBold
                color: Theme.text
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: screen.history.heading.length > 0
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    spacing: 12
                    HeaderText { text: "Checked"; Layout.preferredWidth: 170 }
                    HeaderText { text: "Listing"; Layout.preferredWidth: 160 }
                    HeaderText { text: "Its total"; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                    HeaderText { text: "Median sold then"; Layout.preferredWidth: 140; horizontalAlignment: Text.AlignRight }
                    HeaderText { text: "Flags"; Layout.fillWidth: true }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                }
                Repeater {
                    model: screen.history.checks
                    ColumnLayout {
                        id: check
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.target
                            spacing: 12
                            LinkText {
                                Layout.preferredWidth: 170
                                text: check.modelData.checked
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSize + 1
                                Accessible.name: "Open the lookup of " + check.modelData.checked
                                onActivated: screen.backend.openSaved(check.modelData.lookupId)
                            }
                            LinkText {
                                Layout.preferredWidth: 160
                                visible: check.modelData.itemId.length > 0
                                text: "item " + check.modelData.itemId
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSize + 1
                                elide: Text.ElideRight
                                Accessible.name: "Open item " + check.modelData.itemId + " on eBay"
                                onActivated: screen.backend.openOnEbay(check.modelData.itemId)
                            }
                            Text {
                                Layout.preferredWidth: 160
                                visible: check.modelData.itemId.length === 0
                                text: "-"
                                font.family: Theme.mono
                                color: Theme.mutedText
                            }
                            Text {
                                Layout.preferredWidth: 110
                                horizontalAlignment: Text.AlignRight
                                text: check.modelData.total
                                font.family: Theme.mono
                                font.pixelSize: Theme.bodySize
                                color: Theme.text
                            }
                            Text {
                                Layout.preferredWidth: 140
                                horizontalAlignment: Text.AlignRight
                                text: check.modelData.medianThen
                                font.family: Theme.mono
                                font.pixelSize: Theme.bodySize
                                color: Theme.text
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: check.modelData.flags
                                    Pill {
                                        required property var modelData
                                        text: modelData.text
                                        kind: modelData.kind
                                    }
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.border
                        }
                    }
                }
            }
        }
    }

    // A re-check that could not start (the session reports why).
    Connections {
        target: screen.backend
        function onSessionFailed(message) {
            if (screen.visible)
                screen.error = message
        }
    }
}
