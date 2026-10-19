// The horizontal range bar (ui-spec 2): the track, the middle half of the
// sales as a band, the median tick, THIS listing's marker (amber, labeled
// with its total) and the cheapest-for-sale marker (blue square, labeled).
// Values in cents; -1 = not shown. Drawn with plain Qt Quick items.
import QtQuick

Item {
    id: bar
    property double minimum: 0
    property double maximum: 1
    property double bandLow: -1
    property double bandHigh: -1
    property double median: -1
    property double thisValue: -1
    property string thisLabel
    property double cheapest: -1
    property string cheapestLabel

    readonly property int trackY: 34
    function xOf(value) {
        const span = maximum - minimum
        return span > 0 ? (value - minimum) / span * width : width / 2
    }
    function labelX(value, label) {
        return Math.max(0, Math.min(width - label.width, xOf(value) - label.width / 2))
    }

    implicitHeight: 80
    Accessible.role: Accessible.Chart
    Accessible.name: "Sold totals: middle half, median, this listing " + thisLabel
                     + (cheapestLabel.length > 0 ? ", " + cheapestLabel : "")

    Rectangle {
        y: bar.trackY
        width: parent.width
        height: 10
        radius: 5
        color: Theme.raised
        border.color: Theme.border
    }
    Rectangle {
        visible: bar.bandLow >= 0 && bar.bandHigh >= 0
        x: bar.xOf(bar.bandLow)
        y: bar.trackY
        width: Math.max(2, bar.xOf(bar.bandHigh) - bar.xOf(bar.bandLow))
        height: 10
        radius: 5
        color: Theme.band
    }
    Rectangle {
        visible: bar.median >= 0
        x: bar.xOf(bar.median) - 1
        y: bar.trackY - 6
        width: 2
        height: 22
        color: Theme.text
    }
    // THIS listing: an amber circle with its label above.
    Rectangle {
        visible: bar.thisValue >= 0
        x: bar.xOf(bar.thisValue) - 8
        y: bar.trackY - 3
        width: 16
        height: 16
        radius: 8
        color: Theme.accent
        border.color: Theme.ground
        border.width: 2
    }
    Text {
        id: thisText
        visible: bar.thisValue >= 0
        x: bar.labelX(bar.thisValue, thisText)
        y: 4
        text: bar.thisLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        font.weight: Font.Medium
        color: Theme.accent
    }
    // The cheapest listing for sale: a blue square with its label below.
    Rectangle {
        visible: bar.cheapest >= 0
        x: bar.xOf(bar.cheapest) - 6
        y: bar.trackY - 1
        width: 12
        height: 12
        color: Theme.infoMarker
        border.color: Theme.ground
        border.width: 2
    }
    Text {
        id: cheapestText
        visible: bar.cheapest >= 0
        x: bar.labelX(bar.cheapest, cheapestText)
        y: bar.trackY + 20
        text: bar.cheapestLabel
        font.family: Theme.mono
        font.pixelSize: Theme.labelSize
        color: Theme.infoLabel
    }
}
