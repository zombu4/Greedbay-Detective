// The model is not known with confidence (owner rule 26): nothing is
// searched until the user says what the item is. Shows the listing's title,
// item specifics and a link to it (photos), the app's suggestions, and the
// brand / model / product name fields.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property string title
    property string itemId
    property string reason
    property var specifics: []
    property var suggestions: []
    // A brand with many known models: the first ones (the listing's own
    // codes come first), all on request.
    readonly property int shortList: 12
    property bool showAllSuggestions: false
    onSuggestionsChanged: showAllSuggestions = false
    property string brand
    property string model
    property string productName
    property string error
    signal saveRequested(string brand, string model, string productName)
    signal openListingRequested()
    signal cancelRequested()

    spacing: 10

    Text {
        text: "Which item is this?"
        font.family: Theme.sans
        font.pixelSize: Theme.sectionSize
        font.weight: Font.DemiBold
        color: Theme.text
    }
    Text {
        Layout.fillWidth: true
        text: "The app could not tell the model with confidence" + (panel.reason.length > 0 ? " (" + panel.reason + ")" : "")
              + ". Nothing is searched until you enter it; the app remembers it for this listing."
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    Text {
        Layout.fillWidth: true
        text: panel.title
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.bodySize
        font.weight: Font.Medium
        color: Theme.text
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(specificsColumn.implicitHeight, 120)
        visible: panel.specifics.length > 0
        clip: true
        Column {
            id: specificsColumn
            spacing: 2
            Repeater {
                model: panel.specifics
                Text {
                    required property string modelData
                    text: modelData
                    font.family: Theme.sans
                    font.pixelSize: Theme.labelSize
                    color: Theme.secondaryText
                }
            }
        }
    }
    LinkText {
        visible: panel.itemId.length > 0
        text: "Open the listing on eBay (photos)"
        onActivated: panel.openListingRequested()
    }
    Flow {
        Layout.fillWidth: true
        visible: panel.suggestions.length > 0
        spacing: 8
        Text {
            text: "Suggestions:"
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
        }
        Repeater {
            objectName: "suggestionPills"
            model: panel.showAllSuggestions ? panel.suggestions : panel.suggestions.slice(0, panel.shortList)
            Pill {
                required property string modelData
                text: modelData
                kind: "info"
                Accessible.role: Accessible.Button
                Accessible.name: "Use " + modelData + " as the model"
                Accessible.onPressAction: modelField.text = modelData
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: modelField.text = parent.modelData
                }
            }
        }
        LinkText {
            objectName: "showAllLink"
            visible: !panel.showAllSuggestions && panel.suggestions.length > panel.shortList
            text: "Show all " + panel.suggestions.length
            onActivated: panel.showAllSuggestions = true
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        Text { text: "Brand"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField { id: brandField; Layout.fillWidth: true; text: panel.brand; Accessible.name: "Brand" }
        Text { text: "Model"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField { id: modelField; Layout.fillWidth: true; mono: true; text: panel.model; Accessible.name: "Model" }
        Text { text: "Product name"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField {
            id: productField
            Layout.fillWidth: true
            text: panel.productName
            placeholderText: "optional"
            Accessible.name: "Product name"
        }
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
            text: "Save and search"
            primary: true
            enabled: brandField.text.trim().length > 0 && modelField.text.trim().length > 0
            onClicked: panel.saveRequested(brandField.text, modelField.text, productField.text)
        }
    }
}
