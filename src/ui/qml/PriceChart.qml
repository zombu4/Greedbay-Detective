// The sold-prices-over-time chart (ui-spec 3), drawn with plain Qt Quick
// items (never Qt Charts or Qt Graphs): a dot for each sale's total, the
// middle half of the sales as a band, the median as a dashed line with its
// label, amber dots for the listings the user checked, the money range on
// the left and the window's first and last day below. Times in ms since the
// epoch, money in cents; -1 = not shown.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: chart
    property var sales: []
    property var checks: []
    property double minTime: 0
    property double maxTime: 1
    property double minCents: 0
    property double maxCents: 1
    property double bandLow: -1
    property double bandHigh: -1
    property double median: -1
    property string medianLabel
    property string startLabel
    property string endLabel
    property string lowLabel
    property string highLabel

    readonly property int leftSpace: 84
    readonly property int bottomSpace: 28
    readonly property int plotWidth: Math.max(1, width - leftSpace - 12)
    readonly property int plotHeight: Math.max(1, height - bottomSpace - 12)

    function xOf(time) {
        const span = maxTime - minTime
        return leftSpace + (span > 0 ? (time - minTime) / span * plotWidth : plotWidth / 2)
    }
    function yOf(cents) {
        const span = maxCents - minCents
        return 12 + (span > 0 ? (1 - (cents - minCents) / span) * plotHeight : plotHeight / 2)
    }

    implicitHeight: 320
    Accessible.role: Accessible.Chart
    Accessible.name: "Sold prices over time: " + sales.length + " sales" + (medianLabel.length > 0 ? ", " + medianLabel : "")

    // The plot area.
    Rectangle {
        x: chart.leftSpace
        y: 12
        width: chart.plotWidth
        height: chart.plotHeight
        color: Theme.raised
        border.color: Theme.border
        radius: 6
    }
    // The middle half of the sales.
    Rectangle {
        visible: chart.bandLow >= 0 && chart.bandHigh >= 0
        x: chart.leftSpace
        y: chart.yOf(chart.bandHigh)
        width: chart.plotWidth
        height: Math.max(2, chart.yOf(chart.bandLow) - chart.yOf(chart.bandHigh))
        color: Theme.band
        opacity: 0.55
    }
    // The median: a dashed line with its label at the right end.
    Repeater {
        model: chart.median >= 0 ? Math.floor(chart.plotWidth / 12) : 0
        Rectangle {
            required property int index
            x: chart.leftSpace + index * 12
            y: chart.yOf(chart.median) - 1
            width: 7
            height: 2
            color: Theme.text
        }
    }
    Text {
        visible: chart.median >= 0
        x: chart.leftSpace + chart.plotWidth - width - 6
        y: chart.yOf(chart.median) - height - 4
        text: chart.medianLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.text
    }
    // Each sale.
    Repeater {
        model: chart.sales
        Rectangle {
            id: sale
            required property var modelData
            x: chart.xOf(modelData.time) - 4
            y: chart.yOf(modelData.cents) - 4
            width: 8
            height: 8
            radius: 4
            color: Theme.secondaryText
            Accessible.role: Accessible.StaticText
            Accessible.name: modelData.label
            MouseArea {
                id: saleArea
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
            }
            ToolTip.visible: saleArea.containsMouse
            ToolTip.text: modelData.label
        }
    }
    // The user's checks of the item.
    Repeater {
        model: chart.checks
        Rectangle {
            required property var modelData
            visible: modelData.cents >= 0
            x: chart.xOf(modelData.time) - 7
            y: chart.yOf(modelData.cents) - 7
            width: 14
            height: 14
            radius: 7
            color: Theme.accent
            border.color: Theme.ground
            border.width: 2
            Accessible.role: Accessible.StaticText
            Accessible.name: "Your check " + modelData.checked + ": " + modelData.total
            MouseArea {
                id: checkArea
                anchors.fill: parent
                anchors.margins: -3
                hoverEnabled: true
            }
            ToolTip.visible: checkArea.containsMouse
            ToolTip.text: "Your check " + modelData.checked + ": " + modelData.total
        }
    }
    // The money range and the window's days.
    Text {
        x: chart.leftSpace - width - 8
        y: 12
        text: chart.highLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Text {
        x: chart.leftSpace - width - 8
        y: 12 + chart.plotHeight - height
        text: chart.lowLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Text {
        x: chart.leftSpace
        y: chart.height - height
        text: chart.startLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Text {
        x: chart.leftSpace + chart.plotWidth - width
        y: chart.height - height
        text: chart.endLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
}
