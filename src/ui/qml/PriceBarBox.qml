// "Where this listing sits" (ui-spec 2): the range bar in a box of its own,
// the scale's ends under it and a legend row - dot = this listing (amber),
// square = cheapest Buy It Now (blue), band = middle half of sold, line =
// median.
import QtQuick
import QtQuick.Layouts

StatBox {
    id: box
    required property var results
    heading: "Where this listing sits"

    component Key: RowLayout {
        property string text
        default property alias mark: markHolder.data
        spacing: Theme.unit
        visible: text.length > 0
        Item {
            id: markHolder
            implicitWidth: 16
            implicitHeight: 16
        }
        Text {
            text: parent.text
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.secondaryText
        }
    }

    RangeBar {
        Layout.fillWidth: true
        minimum: box.results.barMin
        maximum: box.results.barMax
        bandLow: box.results.bandLow
        bandHigh: box.results.bandHigh
        median: box.results.medianValue
        thisValue: box.results.thisValue
        thisLabel: box.results.thisLabel
        cheapest: box.results.cheapestValue
        cheapestLabel: box.results.cheapestLabel
    }
    RowLayout {
        Layout.fillWidth: true
        Text {
            text: box.results.scaleLowText
            font.family: Theme.mono
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
            Accessible.name: "Scale from " + text
        }
        Item { Layout.fillWidth: true }
        Text {
            text: box.results.scaleHighText
            font.family: Theme.mono
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
            Accessible.name: "Scale to " + text
        }
    }
    Flow {
        objectName: "barLegend"
        Layout.fillWidth: true
        spacing: 24
        Key {
            text: box.results.thisValue >= 0 ? "this listing, " + box.results.thisLabel.replace(/^THIS /, "") : ""
            Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: Theme.accent }
        }
        Key {
            text: box.results.cheapestValue >= 0 ? "cheapest Buy It Now" : ""
            Rectangle { anchors.centerIn: parent; width: 11; height: 11; color: Theme.infoMarker }
        }
        Key {
            text: box.results.bandLow >= 0 ? box.results.bandLegend : ""
            Rectangle { anchors.centerIn: parent; width: 16; height: 8; radius: 4; color: Theme.band }
        }
        Key {
            text: box.results.medianValue >= 0 ? box.results.medianLegend : ""
            Rectangle { anchors.centerIn: parent; width: 2; height: 16; color: Theme.text }
        }
    }
}
