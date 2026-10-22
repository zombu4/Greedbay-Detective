// One tab's table (ui-spec 2): Use (checkbox) | Where | Total | eBay
// condition | Sold / listed | Format (BUY IT NOW / BEST OFFER / AUCTION - N
// bids, an auction's time left) | Title with CHEAPEST / THIS LISTING pills |
// View (for-sale rows only); the not-this-item rows also show their reason.
// Sorted by total, lowest first (by the model). A title or View opens the
// listing in the system browser.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: table
    required property RowsModel rows
    property bool showReasons: false
    signal rowToggled(int row, bool included)
    signal openRequested(string itemId)
    signal sellerClicked(string seller, double percent, int count)

    readonly property int useWidth: 56
    readonly property int whereWidth: 170
    readonly property int totalWidth: 110
    readonly property int conditionWidth: 190
    readonly property int dateWidth: 110
    readonly property int formatWidth: 170
    readonly property int viewWidth: 76

    spacing: 0

    component HeaderText: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        font.weight: Font.Medium
        color: Theme.mutedText
        elide: Text.ElideRight
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        spacing: 12
        HeaderText { text: "Use"; Layout.preferredWidth: table.useWidth }
        HeaderText { text: "Where"; Layout.preferredWidth: table.whereWidth }
        HeaderText { text: "Total"; Layout.preferredWidth: table.totalWidth; horizontalAlignment: Text.AlignRight }
        HeaderText { text: "eBay condition"; Layout.preferredWidth: table.conditionWidth }
        HeaderText { text: "Sold / listed"; Layout.preferredWidth: table.dateWidth }
        HeaderText { text: "Format"; Layout.preferredWidth: table.formatWidth }
        HeaderText { text: "Title"; Layout.fillWidth: true }
        HeaderText { text: ""; Layout.preferredWidth: table.viewWidth }
    }
    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Theme.border
    }

    Text {
        visible: table.rows.count === 0
        Layout.topMargin: 12
        text: "No rows."
        font.family: Theme.sans
        font.pixelSize: Theme.bodySize
        color: Theme.mutedText
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 560)
        clip: true
        model: table.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: line
            required property int row
            required property bool included
            required property string where
            required property string total
            required property string condition
            required property string date
            required property string title
            required property string itemId
            required property string reason
            required property string mark
            required property string broken
            required property string origin
            required property string originText
            required property string format
            required property string timeLeft
            required property string seller
            required property string sellerBadge
            required property bool sellerMarked
            required property int sellerShared
            required property double sellerPercent
            required property int sellerCount
            required property bool view
            required property string viewNote
            width: list.width
            implicitHeight: Math.max(Theme.target, content.implicitHeight + 12)
            color: mark === "this" ? Theme.warnBackground : "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                id: content
                anchors.fill: parent
                spacing: 12
                CheckBox {
                    Layout.preferredWidth: table.useWidth
                    checked: line.included
                    Accessible.name: (line.included ? "Counted: " : "Not counted: ") + line.title
                    onToggled: table.rowToggled(line.row, checked)
                }
                Text {
                    Layout.preferredWidth: table.whereWidth
                    text: line.where
                    elide: Text.ElideRight
                    font.family: Theme.sans
                    font.pixelSize: Theme.labelSize + 1
                    color: Theme.secondaryText
                }
                Text {
                    Layout.preferredWidth: table.totalWidth
                    horizontalAlignment: Text.AlignRight
                    text: line.total
                    font.family: Theme.mono
                    font.pixelSize: Theme.bodySize
                    color: Theme.text
                }
                Text {
                    Layout.preferredWidth: table.conditionWidth
                    text: line.condition.length > 0 ? line.condition : "-"
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Theme.sans
                    font.pixelSize: Theme.labelSize + 1
                    color: Theme.secondaryText
                }
                Text {
                    Layout.preferredWidth: table.dateWidth
                    text: line.date
                    font.family: Theme.mono
                    font.pixelSize: Theme.labelSize + 1
                    color: Theme.secondaryText
                }
                ColumnLayout {
                    Layout.preferredWidth: table.formatWidth
                    spacing: 2
                    Pill {
                        visible: line.format.length > 0
                        text: line.format
                        kind: "neutral"
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: line.timeLeft.length > 0 ? line.timeLeft : line.viewNote
                        wrapMode: Text.WordWrap
                        font.family: Theme.mono
                        font.pixelSize: Theme.labelSize - 1
                        color: Theme.mutedText
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Pill {
                            visible: line.mark === "this"
                            text: "THIS LISTING"
                            kind: "warn"
                        }
                        Pill {
                            visible: line.mark === "cheapest"
                            text: "CHEAPEST"
                            kind: "info"
                        }
                        Pill {
                            id: brokenPill
                            visible: line.broken.length > 0
                            text: "LIKELY BROKEN"
                            kind: "danger"
                            Accessible.name: "Likely broken: " + line.broken
                            HoverHandler { id: brokenHover }
                            ToolTip.visible: brokenHover.hovered
                            ToolTip.text: "Likely broken: " + line.broken
                        }
                        Pill {
                            id: originPill
                            visible: line.origin.length > 0
                            text: line.origin
                            // China: red; Japan (scalper risk): amber.
                            kind: line.origin.startsWith("CHINESIUM") ? "danger" : "warn"
                            Accessible.name: line.originText
                            HoverHandler { id: originHover }
                            ToolTip.visible: originHover.hovered
                            ToolTip.text: line.originText
                        }
                        LinkText {
                            Layout.fillWidth: true
                            text: line.title.length > 0 ? line.title : "(no title)"
                            elide: Text.ElideRight
                            color: line.itemId.length > 0 ? Theme.text : Theme.mutedText
                            font.pixelSize: Theme.labelSize + 1
                            onActivated: if (line.itemId.length > 0) table.openRequested(line.itemId)
                        }
                    }
                    // The seller, with the user's mark and the rating badge.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: line.seller.length > 0
                        spacing: 8
                        LinkText {
                            text: "seller: " + line.seller
                            elide: Text.ElideRight
                            Layout.maximumWidth: 260
                            font.pixelSize: Theme.labelSize
                            Accessible.name: "Seller " + line.seller + ", open the seller panel"
                            onActivated: table.sellerClicked(line.seller, line.sellerPercent, line.sellerCount)
                        }
                        Pill {
                            visible: line.sellerMarked
                            text: "MARKED SELLER"
                            kind: "danger"
                        }
                        Pill {
                            visible: line.sellerShared > 0
                            text: "SHARED LIST (" + line.sellerShared + ")"
                            kind: "danger"
                        }
                        Pill {
                            visible: line.sellerBadge.length > 0
                            text: line.sellerBadge
                            kind: line.sellerBadge.startsWith("rating") ? "danger" : "neutral"
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: table.showReasons && line.reason.length > 0
                        text: line.reason
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.family: Theme.sans
                        font.pixelSize: Theme.labelSize
                        color: Theme.mutedText
                    }
                }
                // Only for listings for sale now (never sold or Product
                // Research rows, never an auction whose end has passed).
                AppButton {
                    Layout.preferredWidth: table.viewWidth
                    visible: line.view
                    text: "View"
                    leftPadding: 10
                    rightPadding: 10
                    Accessible.name: "View on eBay: " + line.title
                    onClicked: table.openRequested(line.itemId)
                }
                Item {
                    Layout.preferredWidth: table.viewWidth
                    visible: !line.view
                }
            }
        }
    }
}
