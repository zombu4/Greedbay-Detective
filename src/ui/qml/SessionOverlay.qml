// Above every screen while a lookup runs: progress, the browser prompt or
// the ask-for-the-model prompt, depending on the session's state. Blocks the
// screens below it; hidden when the lookup is saved (the Results screen
// shows it) or over.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: overlay
    required property Backend backend
    readonly property LookupSession session: backend.session
    readonly property string sessionState: session.stateName

    visible: sessionState !== "idle" && sessionState !== "closing"
    color: "#d916181b"

    // Clicks and the wheel stop here.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    Card {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 64)
        padding: 24
        line: Theme.inputBorder

        // Only the visible panel takes space.
        ColumnLayout {
            width: parent.width

            ProgressPanel {
                Layout.fillWidth: true
                visible: overlay.sessionState === "starting" || overlay.sessionState === "reading"
                         || overlay.sessionState === "searching"
                steps: overlay.session.steps
                step: overlay.session.step
                pages: overlay.session.pagesThisLookup
                pagesToday: overlay.backend.pagesTodayText
                lastLine: overlay.session.lastLine
                browserName: overlay.session.browserName
                onCancelRequested: overlay.session.cancel()
            }
            BrowserPromptPanel {
                Layout.fillWidth: true
                visible: overlay.sessionState === "waitingForUser"
                message: overlay.session.userMessage
                browserName: overlay.session.browserName
                onContinueRequested: overlay.session.continueAfterUser()
                onCancelRequested: overlay.session.cancel()
            }
            AskModelPanel {
                Layout.fillWidth: true
                visible: overlay.sessionState === "askIdentity"
                title: overlay.session.askTitle
                itemId: overlay.session.askItemId
                reason: overlay.session.askReason
                specifics: overlay.session.askSpecifics
                suggestions: overlay.session.askSuggestions
                brand: overlay.session.askBrand
                model: overlay.session.askModel
                productName: overlay.session.askProductName
                error: overlay.session.errorText
                onSaveRequested: (brand, model, productName) => overlay.session.submitIdentity(brand, model, productName)
                onOpenListingRequested: overlay.backend.openOnEbay(overlay.session.askItemId)
                onCancelRequested: overlay.session.cancel()
            }
        }
    }
}
