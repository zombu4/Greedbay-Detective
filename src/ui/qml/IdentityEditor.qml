// "Wrong item? Correct it" (ui-spec 2): brand, model and product name; Save
// remembers it for this listing and searches again.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: editor
    property string brand
    property string model
    property string productName
    property string error
    signal saveRequested(string brand, string model, string productName)

    function openWith(brandText, modelText, productText) {
        brandField.text = brandText
        modelField.text = modelText
        productField.text = productText
        error = ""
        open()
        brandField.forceActiveFocus()
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 24
    background: Rectangle {
        color: Theme.surface
        radius: Theme.cardRadius
        border.color: Theme.inputBorder
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Text {
            text: "Correct the item"
            font.family: Theme.sans
            font.pixelSize: Theme.sectionSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            text: "Enter the brand and the model as the maker writes them. The app remembers it for this listing "
                  + "and searches again; the new lookup is saved next to this one."
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.labelSize
            color: Theme.mutedText
        }
        Text { text: "Brand"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField { id: brandField; Layout.fillWidth: true; text: editor.brand; Accessible.name: "Brand" }
        Text { text: "Model"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField { id: modelField; Layout.fillWidth: true; mono: true; text: editor.model; Accessible.name: "Model" }
        Text { text: "Product name (optional)"; color: Theme.mutedText; font.family: Theme.sans; font.pixelSize: Theme.labelSize }
        AppTextField { id: productField; Layout.fillWidth: true; text: editor.productName; Accessible.name: "Product name" }
        Text {
            Layout.fillWidth: true
            visible: editor.error.length > 0
            text: editor.error
            wrapMode: Text.WordWrap
            font.family: Theme.sans
            font.pixelSize: Theme.bodySize
            color: Theme.warnLabel
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12
            AppButton { text: "Cancel"; onClicked: editor.close() }
            AppButton {
                text: "Save and search again"
                primary: true
                enabled: brandField.text.trim().length > 0 && modelField.text.trim().length > 0
                onClicked: editor.saveRequested(brandField.text, modelField.text, productField.text)
            }
        }
    }
}
