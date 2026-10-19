// Before the searches run, when the user asked for it on Home: the searches
// (one per line, editable) and how deep this lookup goes (owner decisions
// Q14 and answer 4). Settings keeps the defaults.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property var queries: []
    property int soldDepth: 0
    property int activeDepth: 0
    property int researchDepth: 0
    property string error
    signal searchRequested(string queries, int soldDepth, int activeDepth, int researchDepth)
    signal cancelRequested()

    spacing: 10

    component FieldLabel: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    component Depth: SpinBox {
        from: 0
        to: 100000
        editable: true
        implicitHeight: Theme.target
        font.family: Theme.mono
        font.pixelSize: Theme.bodySize
    }

    Text {
        text: "Check the searches"
        font.family: Theme.sans
        font.pixelSize: Theme.sectionSize
        font.weight: Font.DemiBold
        color: Theme.text
    }
    Text {
        Layout.fillWidth: true
        text: "One search per line, the brand and the model as the maker writes them. Add, change or remove lines; "
              + "how deep each source goes applies to this lookup only (0 skips a source)."
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    TextArea {
        id: queryText
        Layout.fillWidth: true
        Layout.preferredHeight: 110
        text: panel.queries.join("\n")
        wrapMode: TextArea.NoWrap
        font.family: Theme.mono
        font.pixelSize: Theme.bodySize
        color: Theme.text
        Accessible.name: "Searches, one per line"
        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.raised
            border.color: queryText.activeFocus ? Theme.accent : Theme.inputBorder
        }
    }
    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        FieldLabel { text: "Sold listings" }
        Depth { id: sold; value: panel.soldDepth; Accessible.name: "Sold listings" }
        FieldLabel { text: "Listings for sale" }
        Depth { id: active; value: panel.activeDepth; Accessible.name: "Listings for sale" }
        FieldLabel { text: "Product Research rows" }
        Depth { id: research; value: panel.researchDepth; Accessible.name: "Product Research rows" }
    }
    Text {
        Layout.fillWidth: true
        visible: panel.error.length > 0
        text: panel.error
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.bodySize
        color: Theme.warnLabel
    }
    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12
        AppButton {
            text: "Cancel"
            Accessible.name: "Cancel the lookup"
            onClicked: panel.cancelRequested()
        }
        AppButton {
            text: "Search"
            primary: true
            enabled: queryText.text.trim().length > 0
            onClicked: panel.searchRequested(queryText.text, sold.value, active.value, research.value)
        }
    }
}
