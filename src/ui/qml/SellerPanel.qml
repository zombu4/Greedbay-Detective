// The seller panel: the seller's rating, the user's own mark on the seller
// (kept only on this computer), and the seller's negative or neutral reviews
// read from eBay's feedback page on request; the shared seller list's reports on
// the seller, a dispute of one, and a report of your own. Opened with show().
// Text from eBay pages or other users is always shown as plain text.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: panel
    objectName: "sellerPanel"
    required property var backend
    readonly property var sellers: backend.sellers
    readonly property var session: backend.sellers.session
    // The browser read is about the seller shown (another seller's read may
    // still be running or have left its message).
    readonly property bool sessionHere: session.sellerId === sellers.sellerId
    // The mark form is open (a new mark or an edit), and the inline question
    // before a mark is removed.
    property bool editing: false
    property bool confirming: false
    property string formError: ""
    // The shared seller list: this seller's reports (re-read when the list
    // changes), the report form, the report being disputed.
    readonly property var shared: backend.sharedList
    readonly property var sharedReports: shared.revision >= 0 ? shared.reportsFor(sellers.sellerId) : []
    readonly property bool reportHere: shared.reportSellerId === sellers.sellerId
    property bool reporting: false
    property string disputeId: ""

    // Loads the seller (percent / count are -1 when unknown), then opens.
    function show(sellerId, displayName, percent, count) {
        editing = false
        confirming = false
        formError = ""
        reporting = false
        disputeId = ""
        sellers.open(sellerId, displayName, percent, count)
        open()
    }
    function startForm() {
        formError = ""
        confirming = false
        reasonBox.currentIndex = sellers.marked ? Math.max(0, reasonBox.indexOfValue(sellers.markReason)) : 0
        noteArea.text = sellers.marked ? sellers.markNote : ""
        editing = true
        reasonBox.forceActiveFocus()
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, Overlay.overlay ? Overlay.overlay.width - 48 : 720)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 48 : 640, body.implicitHeight + 2 * padding)
    padding: 24
    background: Rectangle {
        color: Theme.surface
        radius: Theme.cardRadius
        border.color: Theme.inputBorder
    }
    Overlay.modal: Rectangle { color: Qt.alpha(Theme.ground, 0.8) }

    component Muted: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: body
            width: scroll.availableWidth
            spacing: 12

            // Heading: the name, and the seller id when it differs.
            Text {
                Layout.fillWidth: true
                text: panel.sellers.displayName.length > 0 ? panel.sellers.displayName : panel.sellers.sellerId
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.sectionSize
                font.weight: Font.DemiBold
                color: Theme.text
                Accessible.role: Accessible.Heading
            }
            Text {
                Layout.fillWidth: true
                visible: panel.sellers.displayName.length > 0 && panel.sellers.sellerId !== panel.sellers.displayName
                text: panel.sellers.sellerId
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                font.family: Theme.mono
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
                Accessible.name: "Seller id " + text
            }

            // The seller's rating and what is wrong with it, if anything.
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: panel.sellers.ratingText
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.text
            }
            FlagCard {
                Layout.fillWidth: true
                visible: panel.sellers.ratingNote.length > 0
                kind: panel.sellers.ratingFlagged ? "danger" : "info"
                label: "SELLER RATING"
                lines: [panel.sellers.ratingNote]
            }

            // The user's own mark.
            FlagCard {
                Layout.fillWidth: true
                visible: panel.sellers.marked
                kind: "danger"
                label: "YOU MARKED THIS SELLER"
                lines: panel.sellers.markNote.length > 0
                       ? [panel.sellers.markReasonLabel + " - " + panel.sellers.markDate, panel.sellers.markNote]
                       : [panel.sellers.markReasonLabel + " - " + panel.sellers.markDate]
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.sellers.marked && !panel.editing
                spacing: 12
                AppButton {
                    visible: !panel.confirming
                    text: "Edit"
                    Accessible.name: "Edit your mark on this seller"
                    onClicked: panel.startForm()
                }
                AppButton {
                    visible: !panel.confirming
                    text: "Unmark"
                    Accessible.name: "Unmark this seller"
                    onClicked: panel.confirming = true
                }
                Text {
                    visible: panel.confirming
                    text: "Remove your mark?"
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: Theme.text
                }
                AppButton {
                    visible: panel.confirming
                    text: "Yes"
                    primary: true
                    Accessible.name: "Yes, remove my mark"
                    onClicked: {
                        panel.confirming = false
                        if (!panel.sellers.unmark(panel.sellers.sellerId))
                            panel.formError = panel.sellers.error
                    }
                }
                AppButton {
                    visible: panel.confirming
                    text: "No"
                    Accessible.name: "No, keep my mark"
                    onClicked: panel.confirming = false
                }
                Item { Layout.fillWidth: true }
            }
            Muted {
                visible: !panel.sellers.canAct
                text: "This lookup was saved before the app kept the seller's eBay user id. Check the listing again "
                      + "to mark the seller or read the reviews."
            }
            AppButton {
                visible: !panel.sellers.marked && !panel.editing
                enabled: panel.sellers.canAct
                text: "Mark seller"
                Accessible.name: "Mark this seller"
                onClicked: panel.startForm()
            }

            // The mark form: a reason, a note, the date.
            Card {
                Layout.fillWidth: true
                visible: panel.editing
                fill: Theme.raised
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Muted { text: "Reason" }
                    ComboBox {
                        id: reasonBox
                        Layout.fillWidth: true
                        implicitHeight: Theme.target
                        model: panel.sellers.reasons
                        textRole: "label"
                        valueRole: "id"
                        Accessible.name: "Reason for the mark"
                    }
                    Muted { text: "Note (optional)" }
                    TextArea {
                        id: noteArea
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        wrapMode: TextEdit.Wrap
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10
                        color: Theme.text
                        placeholderText: "What happened"
                        placeholderTextColor: Theme.mutedText
                        selectionColor: Theme.band
                        selectedTextColor: Theme.text
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        // At most 2000 characters.
                        onTextChanged: if (text.length > 2000) text = text.substring(0, 2000)
                        Accessible.name: "Note about the seller"
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: Theme.surface
                            border.color: noteArea.activeFocus ? Theme.accent : Theme.inputBorder
                            border.width: 1
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Muted { Layout.fillWidth: false; text: "Date" }
                        Text {
                            text: panel.sellers.marked ? panel.sellers.markDate : Qt.formatDate(new Date(), "yyyy-MM-dd")
                            font.family: Theme.mono
                            font.pixelSize: Theme.labelSize
                            color: Theme.secondaryText
                        }
                        Item { Layout.fillWidth: true }
                        Muted {
                            Layout.fillWidth: false
                            text: noteArea.text.length + " / 2000"
                            font.family: Theme.mono
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: panel.formError.length > 0
                        text: panel.formError
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
                            onClicked: {
                                panel.editing = false
                                panel.formError = ""
                            }
                        }
                        AppButton {
                            text: "Save"
                            primary: true
                            enabled: reasonBox.currentIndex >= 0
                            onClicked: {
                                if (panel.sellers.saveMark(reasonBox.currentValue, noteArea.text)) {
                                    panel.editing = false
                                    panel.formError = ""
                                } else {
                                    panel.formError = panel.sellers.error
                                }
                            }
                        }
                    }
                    Muted { text: "Only on this computer - never uploaded or shared." }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: panel.formError.length > 0 && !panel.editing
                text: panel.formError
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.warnLabel
            }

            // The shared seller list: other users' reports on this seller, a
            // dispute of one, and a report of your own.
            Text {
                visible: panel.shared.setUp
                text: "SHARED SELLER LIST"
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: Theme.mutedText
            }
            Muted {
                visible: panel.shared.setUp && !panel.shared.readEnabled
                text: "Reading the shared list is off (Settings, Shared seller list)."
            }
            Muted {
                visible: panel.shared.setUp && panel.shared.readEnabled && panel.sharedReports.length === 0
                text: "No reports on the shared list."
            }
            Repeater {
                model: panel.sharedReports
                Rectangle {
                    id: sharedReport
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: sharedColumn.implicitHeight + 2 * Theme.gap
                    radius: 10
                    color: Theme.raised
                    border.color: Theme.border
                    border.width: 1
                    Accessible.role: Accessible.StaticText
                    Accessible.name: sharedReport.modelData.reasonLabel + ": " + sharedReport.modelData.text
                    ColumnLayout {
                        id: sharedColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.gap
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: sharedReport.modelData.reasonLabel + " - bought " + sharedReport.modelData.month
                                font.family: Theme.sans
                                font.pixelSize: Theme.labelSize
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Pill {
                                visible: sharedReport.modelData.itemChecked
                                text: "Item checked"
                                kind: "neutral"
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                visible: panel.disputeId !== sharedReport.modelData.id
                                text: "Dispute"
                                Accessible.name: "Dispute this report"
                                onClicked: {
                                    disputeArea.text = ""
                                    panel.disputeId = sharedReport.modelData.id
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: sharedReport.modelData.text
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            font.family: Theme.sans
                            font.pixelSize: Theme.bodySize
                            color: Theme.text
                        }
                    }
                }
            }
            // A dispute of one report (to the list owner).
            Card {
                Layout.fillWidth: true
                visible: panel.disputeId.length > 0
                fill: Theme.raised
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Muted { text: "What is wrong with this report? It goes to the list owner, over Tor, without your name." }
                    TextArea {
                        id: disputeArea
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        wrapMode: TextEdit.Wrap
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10
                        color: Theme.text
                        placeholderTextColor: Theme.mutedText
                        selectionColor: Theme.band
                        selectedTextColor: Theme.text
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        onTextChanged: if (text.length > 1000) text = text.substring(0, 1000)
                        Accessible.name: "Why the report is wrong"
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: Theme.surface
                            border.color: disputeArea.activeFocus ? Theme.accent : Theme.inputBorder
                            border.width: 1
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: panel.shared.disputeStatus
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        color: panel.shared.disputeOk ? Theme.secondaryText : Theme.warnLabel
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 12
                        Muted { Layout.fillWidth: false; text: disputeArea.text.length + " / 1000"; font.family: Theme.mono }
                        AppButton {
                            text: "Close"
                            onClicked: panel.disputeId = ""
                        }
                        AppButton {
                            text: "Send"
                            primary: true
                            enabled: !panel.shared.busy && disputeArea.text.trim().length > 0
                            onClicked: panel.shared.dispute(panel.sellers.sellerId, panel.disputeId, disputeArea.text)
                        }
                    }
                }
            }
            AppButton {
                visible: panel.shared.setUp && panel.shared.reportEnabled && !panel.reporting
                         && !(panel.shared.reportBusy && panel.reportHere)
                enabled: panel.sellers.canAct && !panel.shared.busy && !panel.shared.reportBusy
                text: "Report this seller to the shared list"
                Accessible.name: "Report this seller to the shared seller list"
                onClicked: {
                    sharedReason.currentIndex = 0
                    sharedText.text = ""
                    panel.reporting = true
                }
            }
            Card {
                Layout.fillWidth: true
                visible: panel.reporting
                fill: Theme.raised
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Muted {
                        text: "The app first looks for your purchase from this seller on your own eBay Purchases page "
                              + "(in the app's browser, counted in the daily pages). Then it sends only the seller's "
                              + "user id, your reason and words, and that purchase's item number and month - over Tor, "
                              + "without your eBay name. One report per seller."
                    }
                    Muted { text: "Reason" }
                    ComboBox {
                        id: sharedReason
                        Layout.fillWidth: true
                        implicitHeight: Theme.target
                        model: panel.sellers.reasons
                        textRole: "label"
                        valueRole: "id"
                        Accessible.name: "Reason for the report"
                    }
                    Muted { text: "What happened (shown on the shared list)" }
                    TextArea {
                        id: sharedText
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        wrapMode: TextEdit.Wrap
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10
                        color: Theme.text
                        placeholderTextColor: Theme.mutedText
                        selectionColor: Theme.band
                        selectedTextColor: Theme.text
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        onTextChanged: if (text.length > 500) text = text.substring(0, 500)
                        Accessible.name: "What happened with this seller"
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: Theme.surface
                            border.color: sharedText.activeFocus ? Theme.accent : Theme.inputBorder
                            border.width: 1
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 12
                        Muted { Layout.fillWidth: false; text: sharedText.text.length + " / 500"; font.family: Theme.mono }
                        AppButton {
                            text: "Cancel"
                            onClicked: panel.reporting = false
                        }
                        AppButton {
                            text: "Check purchase and send"
                            primary: true
                            enabled: sharedReason.currentIndex >= 0 && sharedText.text.trim().length > 0
                            onClicked: {
                                panel.reporting = false
                                panel.shared.report(panel.sellers.sellerId, sharedReason.currentValue, sharedText.text)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.reportHere && panel.shared.reportStatus.length > 0
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: panel.shared.reportStatus
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: panel.shared.reportBusy || panel.shared.reportOk ? Theme.text : Theme.warnLabel
                }
                AppButton {
                    visible: panel.shared.reportBusy
                    text: "Stop"
                    Accessible.name: "Stop the report"
                    onClicked: panel.shared.cancelReport()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.reportHere && panel.shared.purchaseMessage.length > 0
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: panel.shared.purchaseMessage
                    wrapMode: Text.WordWrap
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: Theme.warnLabel
                }
                AppButton {
                    text: "Continue"
                    primary: true
                    Accessible.name: "Continue the purchase check"
                    onClicked: panel.shared.continuePurchaseCheck()
                }
            }

            // Reviews, read from eBay's feedback page on request.
            Text {
                text: "REVIEWS"
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: Theme.mutedText
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                AppButton {
                    text: "Show negative reviews"
                    enabled: !panel.session.busy && panel.sellers.canAct
                    onClicked: panel.sellers.showReviews("negative", false)
                }
                AppButton {
                    text: "Show neutral reviews"
                    enabled: !panel.session.busy && panel.sellers.canAct
                    onClicked: panel.sellers.showReviews("neutral", false)
                }
                AppButton {
                    visible: panel.sellers.reviewKind.length > 0
                    text: "Refresh"
                    enabled: !panel.session.busy
                    Accessible.name: "Read the " + panel.sellers.reviewKind + " reviews from eBay again"
                    onClicked: panel.sellers.showReviews(panel.sellers.reviewKind, true)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.session.busy && panel.sessionHere
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: "Reading eBay's feedback page..."
                    wrapMode: Text.WordWrap
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: Theme.text
                }
                AppButton {
                    text: "Stop"
                    Accessible.name: "Stop reading the feedback page"
                    onClicked: panel.session.cancel()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.session.waitingForUser && panel.sessionHere
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: panel.session.userMessage
                    wrapMode: Text.WordWrap
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: Theme.warnLabel
                }
                AppButton {
                    text: "Continue"
                    primary: true
                    Accessible.name: "Continue reading the feedback page"
                    onClicked: panel.session.continueAfterUser()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: panel.sellers.reviewSummary
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Muted {
                visible: panel.sellers.readText.length > 0
                text: panel.sellers.readText
                      + (panel.sessionHere && panel.session.pagesUsed > 0 ? " - " + panel.session.pagesUsed + " eBay "
                         + (panel.session.pagesUsed === 1 ? "page" : "pages") + " used this time" : "")
            }
            Repeater {
                model: panel.sellers.reviews
                Rectangle {
                    id: review
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: reviewColumn.implicitHeight + 2 * Theme.gap
                    radius: 10
                    color: Theme.raised
                    border.color: Theme.border
                    border.width: 1
                    Accessible.role: Accessible.StaticText
                    Accessible.name: review.modelData.when + ": " + review.modelData.comment
                    ColumnLayout {
                        id: reviewColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.gap
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: review.modelData.when
                                textFormat: Text.PlainText
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSize
                                color: Theme.mutedText
                            }
                            Pill {
                                visible: review.modelData.verified
                                text: "Verified purchase"
                                kind: "neutral"
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: review.modelData.comment
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            font.family: Theme.sans
                            font.pixelSize: Theme.bodySize
                            color: Theme.text
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: review.modelData.item
                                textFormat: Text.PlainText
                                font.family: Theme.sans
                                font.pixelSize: Theme.labelSize
                                color: Theme.mutedText
                            }
                            Text {
                                visible: review.modelData.price.length > 0
                                text: review.modelData.price
                                textFormat: Text.PlainText
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSize
                                color: Theme.mutedText
                            }
                        }
                    }
                }
            }
            Muted {
                visible: panel.sellers.reviewKind.length > 0 && !panel.session.busy && !panel.session.waitingForUser
                         && panel.sellers.reviews.length === 0 && panel.sellers.readText.length > 0
                         && panel.sellers.error.length === 0 && (!panel.sessionHere || panel.session.error.length === 0)
                text: "No reviews of this kind in the last 12 months."
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: panel.sellers.error
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.warnLabel
            }
            Text {
                Layout.fillWidth: true
                visible: panel.sessionHere && text.length > 0
                text: panel.session.error
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
                color: Theme.warnLabel
            }

            AppButton {
                Layout.alignment: Qt.AlignRight
                text: "Close"
                onClicked: panel.close()
            }
        }
    }
}
