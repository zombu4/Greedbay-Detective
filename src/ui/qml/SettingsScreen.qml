// Settings (inbox phase 6 step 7, PLAN 14): browser, work folder, page pace
// and daily cap, search depth, sold history window, the value numbers,
// update check, sign out of eBay; export and uninstall come in a later
// phase. Every Save stores at once; a refusal says why.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: screen
    required property SettingsController settings

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

    function report(result, target) {
        target.ok = result.length === 0
        target.message = result.length === 0 ? "Saved." : result
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
                heading: "Your data"
                note: "Coming in a later phase."
                RowLayout {
                    spacing: 12
                    AppButton { text: "Export data"; enabled: false }
                    AppButton { text: "Uninstall"; enabled: false }
                }
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
}
