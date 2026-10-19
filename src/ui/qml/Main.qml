// The app window (ui-spec): header on every screen, then Home (Lookups),
// Results, Price history or Settings; the lookup overlay above them while a
// lookup runs. A listing link pasted (Cmd/Ctrl+V outside a text field) or
// dropped anywhere starts a check.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    required property Backend backend

    // 0 Home, 1 Results, 2 Price history, 3 Settings.
    property int screen: 0

    function checkListing(text) {
        const listing = backend.listingFromText(text)
        if (listing.length === 0)
            return false
        screen = 0
        home.setInput(listing)
        if (backend.check(listing, home.reviewFirst))
            return true
        home.showError(backend.session.errorText)
        return false
    }

    width: 1240
    height: 880
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: backend.appName
    color: Theme.ground
    font.family: Theme.sans
    font.pixelSize: Theme.bodySize
    palette.window: Theme.ground
    palette.windowText: Theme.text
    palette.base: Theme.raised
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.placeholderText: Theme.mutedText
    palette.mid: Theme.inputBorder
    palette.dark: Theme.border
    palette.light: Theme.raised
    palette.midlight: Theme.border
    palette.shadow: Theme.ground
    palette.toolTipBase: Theme.raised
    palette.toolTipText: Theme.text

    header: AppHeader {
        appName: window.backend.appName
        tagline: window.backend.tagline
        current: window.screen <= 1 ? 0 : window.screen - 1
        onHomeRequested: window.screen = 0
        onNavigate: (index) => window.screen = index === 0 ? 0 : index + 1
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: window.screen
        HomeScreen {
            id: home
            backend: window.backend
        }
        ResultsScreen {
            backend: window.backend
        }
        PriceHistoryScreen {}
        SettingsScreen {
            settings: window.backend.settings
        }
    }

    Connections {
        target: window.backend
        function onShowResults() { window.screen = 1 }
        function onOpenFailed(message) {
            window.screen = 0
            home.showError(message)
        }
    }

    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: !window.backend.session.running
        onActivated: window.checkListing(window.backend.clipboardText())
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            const text = drop.hasUrls ? drop.urls[0].toString() : drop.text
            if (window.checkListing(text))
                drop.accept()
        }
    }

    SessionOverlay {
        anchors.fill: parent
        backend: window.backend
    }
}
