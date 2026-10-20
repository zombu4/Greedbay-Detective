// Settings (inbox phase 6 step 7, PLAN 14): browser, work folder, page pace
// and daily cap, search depth, sold history window, the value numbers,
// update check, sign out of eBay, plugins (DESIGN 5), export and uninstall
// (DESIGN 8, PLAN 9). Every Save stores at once; a refusal says why.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: screen
    required property SettingsController settings
    required property Backend backend
    readonly property PluginsController plugins: backend.plugins

    // A plugin the app turned off after a failure (during a lookup or on the
    // Results screen) shows as off, with why, whenever Settings is opened.
    onVisibleChanged: {
        if (visible) {
            plugins.refresh()
            backend.sellers.refreshMarks()
        }
    }

    component Section: Card {
        id: section
        property string heading
        property string note
        default property alias body: inner.data
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: section.heading
                font.family: Theme.sans
                font.pixelSize: Theme.sectionSize
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: section.note
                wrapMode: Text.WordWrap
                font.family: Theme.sans
                font.pixelSize: Theme.labelSize
                color: Theme.mutedText
            }
            ColumnLayout {
                id: inner
                Layout.fillWidth: true
                spacing: 10
            }
        }
    }
    component FieldLabel: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
    }
    component Result: Text {
        property string message
        property bool ok: true
        Layout.fillWidth: true
        visible: message.length > 0
        text: message
        wrapMode: Text.WordWrap
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: ok ? Theme.secondaryText : Theme.warnLabel
    }
    component NumberBox: SpinBox {
        editable: true
        implicitHeight: Theme.target
        font.family: Theme.mono
        font.pixelSize: Theme.bodySize
    }

    // `done`: what to say when it worked (default "Saved.").
    function report(result, target, done) {
        target.ok = result.length === 0
        target.message = result.length === 0 ? (done || "Saved.") : result
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            x: 28
            width: Math.min(scroll.availableWidth - 56, 900)
            spacing: Theme.gap

            Item { Layout.preferredHeight: 4 }

            Text {
                text: "Settings"
                font.family: Theme.sans
                font.pixelSize: Theme.headingSize
                font.weight: Font.DemiBold
                color: Theme.text
            }

            Section {
                heading: "Browser"
                note: "The app drives a browser installed on this computer, in its own profile inside the work folder; "
                      + "your own browser profile is never touched."
                RowLayout {
                    spacing: 12
                    ComboBox {
                        id: browserBox
                        Layout.preferredWidth: 320
                        implicitHeight: Theme.target
                        model: screen.settings.browsers
                        textRole: "name"
                        valueRole: "id"
                        Component.onCompleted: currentIndex = indexOfValue(screen.settings.preferredBrowser)
                        onActivated: screen.report(screen.settings.setPreferredBrowser(currentValue), browserResult)
                        Accessible.name: "Browser"
                    }
                    AppButton {
                        text: "Look again"
                        onClicked: {
                            screen.settings.refreshBrowsers()
                            browserBox.currentIndex = browserBox.indexOfValue(screen.settings.preferredBrowser)
                        }
                    }
                }
                FieldLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "In use: " + screen.settings.browserInUse }
                Result { id: browserResult }
            }

            Section {
                heading: "Work folder"
                note: "Everything the app stores is in this folder: the saved lookups, settings, logs and the browser profile."
                TextEdit {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: screen.settings.workFolder
                    font.family: Theme.mono
                    font.pixelSize: Theme.bodySize
                    color: Theme.text
                    selectionColor: Theme.band
                    Accessible.name: "Work folder"
                }
            }

            Section {
                heading: "eBay pages"
                note: "The app waits a random time in this range before each eBay page and loads at most the daily cap."
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    FieldLabel { text: "Shortest wait (seconds)" }
                    NumberBox { id: delayMin; from: 1; to: 600; value: screen.settings.pageDelayMinSeconds; Accessible.name: "Shortest wait in seconds" }
                    FieldLabel { text: "Longest wait (seconds)" }
                    NumberBox { id: delayMax; from: 1; to: 600; value: screen.settings.pageDelayMaxSeconds; Accessible.name: "Longest wait in seconds" }
                    FieldLabel { text: "Pages per day at most" }
                    NumberBox { id: cap; from: 1; to: 100000; value: screen.settings.dailyPageCap; Accessible.name: "Daily page cap" }
                }
                AppButton {
                    text: "Save"
                    onClicked: {
                        let result = screen.settings.setPageDelaySeconds(delayMin.value, delayMax.value)
                        if (result.length === 0)
                            result = screen.settings.setDailyPageCap(cap.value)
                        screen.report(result, paceResult)
                    }
                }
                Result { id: paceResult }
            }

            Section {
                heading: "Search depth"
                note: "How many rows a lookup reads at most from each source. 0 skips a source. To change it for one "
                      + "lookup, tick \"Let me check the searches\" on the Lookups screen."
                RadioButton {
                    id: standard
                    text: screen.settings.standardPresetText
                    checked: screen.settings.depthPreset === "standard"
                    onClicked: screen.report(screen.settings.useStandardDepths(), depthResult)
                }
                RadioButton {
                    id: custom
                    text: "Custom"
                    checked: screen.settings.depthPreset === "custom"
                }
                GridLayout {
                    visible: custom.checked
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    FieldLabel { text: "Sold listings" }
                    NumberBox { id: soldDepth; from: 0; to: 100000; value: screen.settings.soldDepth; Accessible.name: "Sold listings" }
                    FieldLabel { text: "Listings for sale" }
                    NumberBox { id: activeDepth; from: 0; to: 100000; value: screen.settings.activeDepth; Accessible.name: "Listings for sale" }
                    FieldLabel { text: "Product Research rows" }
                    NumberBox { id: researchDepth; from: 0; to: 100000; value: screen.settings.researchDepth; Accessible.name: "Product Research rows" }
                }
                AppButton {
                    visible: custom.checked
                    text: "Save"
                    onClicked: screen.report(screen.settings.setDepths(soldDepth.value, activeDepth.value, researchDepth.value), depthResult)
                }
                Result { id: depthResult }
            }

            Section {
                heading: "Sold history window"
                note: "The period Product Research covers, in days: " + screen.settings.maxHistoryDays
                      + " (3 years) by default and at most; pick a shorter period if you want. The sold search "
                      + "always shows eBay's own period."
                RowLayout {
                    spacing: 12
                    NumberBox { id: days; from: 1; to: screen.settings.maxHistoryDays; value: screen.settings.historyDays; Accessible.name: "Days" }
                    AppButton { text: "Save"; onClicked: screen.report(screen.settings.setHistoryDays(days.value), daysResult) }
                }
                Result { id: daysResult }
            }

            Section {
                heading: "Numbers"
                note: "Sales outside the middle half by more than the factor times its width are outliers and not "
                      + "counted. Confidence by the number of sales counted. Shipping is flagged when it is more than "
                      + "the ratio times the normal shipping AND more than the amount above it."
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    FieldLabel { text: "Outlier factor" }
                    AppTextField {
                        id: outlier; mono: true; Layout.preferredWidth: 160
                        text: screen.settings.outlierFactor.toString()
                        validator: DoubleValidator { bottom: 0.01; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
                        Accessible.name: "Outlier factor"
                    }
                    FieldLabel { text: "High confidence from (sales)" }
                    NumberBox { id: high; from: 1; to: 10000; value: screen.settings.confidenceHigh; Accessible.name: "High confidence from" }
                    FieldLabel { text: "Medium confidence from (sales)" }
                    NumberBox { id: medium; from: 1; to: 10000; value: screen.settings.confidenceMedium; Accessible.name: "Medium confidence from" }
                    FieldLabel { text: "Low confidence from (sales)" }
                    NumberBox { id: low; from: 1; to: 10000; value: screen.settings.confidenceLow; Accessible.name: "Low confidence from" }
                    FieldLabel { text: "Shipping ratio" }
                    AppTextField {
                        id: ratio; mono: true; Layout.preferredWidth: 160
                        text: screen.settings.shippingRatio.toString()
                        validator: DoubleValidator { bottom: 0.01; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
                        Accessible.name: "Shipping ratio"
                    }
                    FieldLabel { text: "Shipping amount above ($)" }
                    AppTextField {
                        id: above; mono: true; Layout.preferredWidth: 160
                        text: screen.settings.shippingMinAboveDollars.toFixed(2)
                        validator: DoubleValidator { bottom: 0; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
                        Accessible.name: "Shipping amount above in dollars"
                    }
                }
                AppButton {
                    text: "Save"
                    onClicked: screen.report(screen.settings.setNumbers(Number(outlier.text), high.value, medium.value, low.value,
                                                                        Number(ratio.text), Number(above.text)), numbersResult)
                }
                Result { id: numbersResult }
            }

            Section {
                objectName: "sellerSettings"
                heading: "Sellers"
                note: "Seller ratings are read from the listing. Marks and read reviews are kept only on this computer, "
                      + "never uploaded or shared."
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    FieldLabel { text: "Flag sellers below (% positive)" }
                    AppTextField {
                        id: sellerThreshold; mono: true; Layout.preferredWidth: 160
                        text: screen.settings.sellerRatingThreshold.toString()
                        validator: DoubleValidator { bottom: 0; top: 100; decimals: 1; notation: DoubleValidator.StandardNotation; locale: "C" }
                        Accessible.name: "Flag sellers below this percent positive"
                    }
                    FieldLabel { text: "Don't judge with fewer ratings than" }
                    NumberBox { id: sellerFew; from: 0; to: 1000000; value: screen.settings.sellerFewRatings; Accessible.name: "Do not judge sellers with fewer ratings than" }
                    FieldLabel { text: "Keep read reviews (days, 0 = always read again)" }
                    NumberBox { id: sellerCache; from: 0; to: 365; value: screen.settings.sellerFeedbackCacheDays; Accessible.name: "Days to keep read reviews, 0 reads again every time" }
                }
                AppButton {
                    text: "Save"
                    onClicked: screen.report(screen.settings.setSellerRules(Number(sellerThreshold.text), sellerFew.value,
                                                                            sellerCache.value), sellerResult)
                }
                Result { id: sellerResult }

                Text {
                    Layout.topMargin: 8
                    text: "My marked sellers"
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize + 1
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                ColumnLayout {
                    objectName: "markedSellersList"
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel {
                        visible: screen.backend.sellers.marks.length === 0
                        text: "No marked sellers."
                    }
                    Repeater {
                        model: screen.backend.sellers.marks
                        Card {
                            id: markRow
                            required property var modelData
                            property bool editing: false
                            property bool confirming: false
                            property string error: ""
                            Layout.fillWidth: true
                            fill: Theme.raised
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: markRow.modelData.sellerId
                                        elide: Text.ElideRight
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.bodySize
                                        color: Theme.text
                                        Accessible.name: "Seller id " + text
                                    }
                                    Pill { text: markRow.modelData.reasonLabel; kind: "danger" }
                                    Text {
                                        text: markRow.modelData.date
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.labelSize
                                        color: Theme.mutedText
                                    }
                                }
                                FieldLabel {
                                    Layout.fillWidth: true
                                    visible: markRow.modelData.note.length > 0
                                    wrapMode: Text.WordWrap
                                    text: markRow.modelData.note
                                    color: Theme.secondaryText
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: !markRow.editing
                                    spacing: 12
                                    AppButton {
                                        visible: !markRow.confirming
                                        text: "Edit"
                                        Accessible.name: "Edit the mark on " + markRow.modelData.sellerId
                                        onClicked: {
                                            markReason.currentIndex = Math.max(0, markReason.indexOfValue(markRow.modelData.reason))
                                            markNote.text = markRow.modelData.note
                                            markRow.error = ""
                                            markRow.editing = true
                                        }
                                    }
                                    AppButton {
                                        visible: !markRow.confirming
                                        text: "Unmark"
                                        Accessible.name: "Unmark " + markRow.modelData.sellerId
                                        onClicked: markRow.confirming = true
                                    }
                                    Text {
                                        visible: markRow.confirming
                                        text: "Remove your mark?"
                                        font.family: Theme.sans
                                        font.pixelSize: Theme.bodySize
                                        color: Theme.text
                                    }
                                    AppButton {
                                        visible: markRow.confirming
                                        text: "Yes"
                                        primary: true
                                        Accessible.name: "Yes, remove the mark on " + markRow.modelData.sellerId
                                        onClicked: {
                                            markRow.confirming = false
                                            if (!screen.backend.sellers.unmark(markRow.modelData.sellerId))
                                                markRow.error = screen.backend.sellers.error
                                        }
                                    }
                                    AppButton {
                                        visible: markRow.confirming
                                        text: "No"
                                        Accessible.name: "No, keep the mark on " + markRow.modelData.sellerId
                                        onClicked: markRow.confirming = false
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                // The inline edit: a reason and a note.
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: markRow.editing
                                    spacing: 12
                                    ComboBox {
                                        id: markReason
                                        Layout.preferredWidth: 260
                                        implicitHeight: Theme.target
                                        model: screen.backend.sellers.reasons
                                        textRole: "label"
                                        valueRole: "id"
                                        Accessible.name: "Reason for the mark on " + markRow.modelData.sellerId
                                    }
                                    AppTextField {
                                        id: markNote
                                        Layout.fillWidth: true
                                        maximumLength: 2000
                                        placeholderText: "Note (optional)"
                                        Accessible.name: "Note for the mark on " + markRow.modelData.sellerId
                                    }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignRight
                                    visible: markRow.editing
                                    spacing: 12
                                    AppButton {
                                        text: "Cancel"
                                        onClicked: {
                                            markRow.editing = false
                                            markRow.error = ""
                                        }
                                    }
                                    AppButton {
                                        text: "Save"
                                        primary: true
                                        enabled: markReason.currentIndex >= 0
                                        Accessible.name: "Save the mark on " + markRow.modelData.sellerId
                                        onClicked: {
                                            if (screen.backend.sellers.saveMarkFor(markRow.modelData.sellerId,
                                                                                   markReason.currentValue, markNote.text)) {
                                                markRow.editing = false
                                                markRow.error = ""
                                            } else {
                                                markRow.error = screen.backend.sellers.error
                                            }
                                        }
                                    }
                                }
                                Result { message: markRow.error; ok: false }
                            }
                        }
                    }
                }
            }

            Section {
                heading: "Updates"
                note: "available after the first release"
                // Owner decision (a), 2026-10-19: shown, greyed out, until a
                // release exists; nothing checks anything yet.
                CheckBox {
                    text: "Check for a new version"
                    checked: screen.settings.updateCheck
                    enabled: false
                }
            }

            Section {
                heading: "eBay sign-in"
                note: "Signing out deletes the app's browser profile (the eBay sign-in, cookies and cache) from the work "
                      + "folder. Your own browser is not touched."
                AppButton {
                    text: "Sign out of eBay"
                    onClicked: {
                        const result = screen.settings.signOut()
                        signOutResult.ok = result.ok
                        signOutResult.message = result.message
                    }
                }
                Result { id: signOutResult }
            }

            Section {
                heading: "Plugins"
                note: "Plugins add rules, figures, warnings or file formats. Each one runs in a sandbox: it gets only the "
                      + "data its permissions name, cannot read your files or go online, and is stopped after the time "
                      + "limit. A plugin is off until you turn it on; one that fails is turned off and says why."
                Repeater {
                    model: screen.plugins.plugins
                    Card {
                        id: plugin
                        required property var modelData
                        Layout.fillWidth: true
                        fill: Theme.raised
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: plugin.modelData.name
                                    elide: Text.ElideRight
                                    font.family: Theme.sans
                                    font.pixelSize: Theme.bodySize + 1
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                                Pill {
                                    text: plugin.modelData.builtIn ? "BUILT-IN EXAMPLE" : "INSTALLED"
                                    kind: "neutral"
                                }
                                Pill {
                                    text: plugin.modelData.enabled ? "ON" : "OFF"
                                    kind: plugin.modelData.enabled ? "accent" : "neutral"
                                }
                                AppButton {
                                    text: plugin.modelData.enabled ? "Turn off" : "Turn on..."
                                    Accessible.name: (plugin.modelData.enabled ? "Turn off " : "Turn on ") + plugin.modelData.name
                                    onClicked: {
                                        if (plugin.modelData.enabled)
                                            screen.report(screen.plugins.setEnabled(plugin.modelData.id, false), pluginsResult, "Turned off.")
                                        else
                                            enableDialog.ask(plugin.modelData)
                                    }
                                }
                                AppButton {
                                    visible: !plugin.modelData.builtIn
                                    text: "Remove"
                                    Accessible.name: "Remove " + plugin.modelData.name
                                    onClicked: screen.report(screen.plugins.remove(plugin.modelData.id), pluginsResult, "Removed.")
                                }
                            }
                            FieldLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Version " + plugin.modelData.version + " by " + plugin.modelData.author + ". "
                                      + plugin.modelData.typeText + "."
                            }
                            FieldLabel {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                wrapMode: Text.WordWrap
                                text: plugin.modelData.description
                                color: Theme.secondaryText
                            }
                            Result {
                                message: plugin.modelData.failure.length > 0
                                         ? "Turned off after a problem: " + plugin.modelData.failure : ""
                                ok: false
                            }
                        }
                    }
                }
                Repeater {
                    model: screen.plugins.problems
                    Result {
                        required property string modelData
                        message: modelData
                        ok: false
                    }
                }
                // Install: drop a plugin folder or .zip here, or type its path.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    radius: Theme.controlRadius
                    color: drop.containsDrag ? Theme.border : "transparent"
                    border.color: drop.containsDrag ? Theme.accent : Theme.inputBorder
                    FieldLabel {
                        anchors.centerIn: parent
                        text: "Drop a plugin folder or .zip file here to install it"
                    }
                    DropArea {
                        id: drop
                        anchors.fill: parent
                        onDropped: (event) => {
                            if (!event.hasUrls)
                                return
                            event.accept()
                            screen.report(screen.plugins.install(event.urls[0].toString()), pluginsResult,
                                          "Installed. It is off until you turn it on.")
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    AppTextField {
                        id: pluginPath
                        Layout.fillWidth: true
                        mono: true
                        placeholderText: "or type the path of a plugin folder or .zip file"
                        Accessible.name: "Path of a plugin folder or zip file"
                        onAccepted: installButton.clicked()
                    }
                    AppButton {
                        id: installButton
                        text: "Install"
                        enabled: pluginPath.text.trim().length > 0
                        onClicked: screen.report(screen.plugins.install(pluginPath.text), pluginsResult,
                                                 "Installed. It is off until you turn it on.")
                    }
                }
                FieldLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Installed plugins are kept in " + screen.plugins.pluginsFolder + "."
                }
                RowLayout {
                    spacing: 12
                    FieldLabel { text: "Time limit per plugin call (ms)" }
                    NumberBox {
                        id: timeLimit
                        from: screen.plugins.minTimeLimitMs
                        to: screen.plugins.maxTimeLimitMs
                        stepSize: 100
                        value: screen.plugins.timeLimitMs
                        Accessible.name: "Time limit per plugin call in milliseconds"
                    }
                    AppButton {
                        text: "Save"
                        onClicked: screen.report(screen.plugins.setTimeLimitMs(timeLimit.value), pluginsResult)
                    }
                }
                Result { id: pluginsResult }
            }

            Section {
                heading: "Your data"
                note: "Export saves every lookup in your Downloads folder: a CSV list, and a JSON file with every row. "
                      + "Uninstall removes the app and what it saved; you choose whether your saved lookups stay."
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    AppButton {
                        text: "Export data"
                        onClicked: {
                            const reply = screen.backend.exportData()
                            exportResult.ok = reply.ok
                            exportResult.message = reply.message
                            exportResult.path = reply.path
                        }
                    }
                    LinkText {
                        visible: exportResult.path.length > 0
                        text: "Show in folder"
                        onActivated: screen.backend.showFolderOf(exportResult.path)
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: "Uninstall..."
                        onClicked: uninstallDialog.open()
                    }
                }
                Result {
                    id: exportResult
                    property string path
                }
                Result { id: uninstallResult }
            }

            Section {
                heading: "Licenses"
                note: "The app's text uses IBM Plex Sans and IBM Plex Mono, licensed under the SIL Open Font License 1.1."
                AppButton {
                    text: license.visible ? "Hide the font license" : "Show the font license"
                    onClicked: license.visible = !license.visible
                }
                TextEdit {
                    id: license
                    Layout.fillWidth: true
                    visible: false
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    text: screen.settings.fontLicense
                    font.family: Theme.mono
                    font.pixelSize: Theme.labelSize
                    color: Theme.secondaryText
                }
            }

            Item { Layout.preferredHeight: 24 }
        }
    }

    // Turning a plugin on: its permissions first, in plain words.
    Dialog {
        id: enableDialog
        property var plugin: ({})
        function ask(entry) {
            plugin = entry
            open()
        }
        anchors.centerIn: parent
        width: Math.min(560, screen.width - 56)
        modal: true
        title: "Turn on " + (plugin.name || "")
        background: Rectangle {
            color: Theme.surface
            radius: Theme.cardRadius
            border.color: Theme.border
        }
        header: Text {
            padding: 20
            text: enableDialog.title
            font.family: Theme.sans
            font.pixelSize: Theme.sectionSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        contentItem: ColumnLayout {
            spacing: 8
            FieldLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "This plugin may:"
            }
            Repeater {
                model: enableDialog.plugin.permissions || []
                Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "- " + modelData
                    wrapMode: Text.WordWrap
                    font.family: Theme.sans
                    font.pixelSize: Theme.bodySize
                    color: Theme.text
                }
            }
            FieldLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "It cannot read your files, go online or start programs."
            }
        }
        footer: RowLayout {
            spacing: 12
            Item { Layout.fillWidth: true }
            AppButton {
                Layout.bottomMargin: 20
                text: "Cancel"
                onClicked: enableDialog.close()
            }
            AppButton {
                Layout.rightMargin: 20
                Layout.bottomMargin: 20
                text: "Turn on"
                primary: true
                onClicked: {
                    enableDialog.close()
                    screen.report(screen.plugins.setEnabled(enableDialog.plugin.id, true), pluginsResult, "Turned on.")
                }
            }
        }
    }

    // Uninstall (PLAN 9): keep the saved lookups and history, or delete
    // everything; what happens to the program on this system.
    Dialog {
        id: uninstallDialog
        anchors.centerIn: parent
        width: Math.min(600, screen.width - 56)
        modal: true
        title: "Uninstall Greedbay Detective"
        background: Rectangle {
            color: Theme.surface
            radius: Theme.cardRadius
            border.color: Theme.border
        }
        header: Text {
            padding: 20
            text: uninstallDialog.title
            font.family: Theme.sans
            font.pixelSize: Theme.sectionSize
            font.weight: Font.DemiBold
            color: Theme.text
        }
        contentItem: ColumnLayout {
            spacing: 10
            RadioButton {
                id: keepHistory
                checked: true
                text: "Keep my saved lookups and history"
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
            }
            FieldLabel {
                Layout.fillWidth: true
                Layout.leftMargin: 32
                wrapMode: Text.WordWrap
                text: "Kept: the saved lookups (with your marked sellers and the seller reviews read), their backups, and "
                      + "the brands and models you confirmed; installing the "
                      + "app again uses them. Deleted: settings, logs, the app's browser profiles with your eBay "
                      + "sign-in and cookies, installed plugins and temporary files."
            }
            RadioButton {
                text: "Delete everything the app saved"
                font.family: Theme.sans
                font.pixelSize: Theme.bodySize
            }
            FieldLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: screen.backend.uninstallNote + " Files you exported to Downloads stay."
                color: Theme.secondaryText
            }
        }
        footer: RowLayout {
            spacing: 12
            Item { Layout.fillWidth: true }
            AppButton {
                Layout.bottomMargin: 20
                text: "Cancel"
                onClicked: uninstallDialog.close()
            }
            AppButton {
                Layout.rightMargin: 20
                Layout.bottomMargin: 20
                text: "Uninstall"
                primary: true
                onClicked: {
                    uninstallDialog.close()
                    const refusal = screen.backend.uninstall(keepHistory.checked)
                    uninstallResult.ok = refusal.length === 0
                    uninstallResult.message = refusal
                }
            }
        }
    }
}
