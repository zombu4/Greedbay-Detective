// Results (ui-spec 2) of the lookup shown: identity bar, listing card, flag
// cards, what the same item sold for (range bar and key numbers), and the
// two tabs of rows. Unticking or ticking a row recomputes everything at once
// (ResultsController::setIncluded).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: screen
    required property Backend backend
    readonly property ResultsController results: backend.results
    property int tab: 0

    // Opens the seller panel for the listing's own seller: by its eBay user
    // id only (the shown name can be a store name); lookups saved before the
    // id was kept have none, and the panel then cannot mark or read reviews.
    function openSeller() {
        sellerPanel.show(screen.results.sellerId, screen.results.seller, screen.results.sellerPercent,
                         screen.results.sellerCount)
    }

    component Muted: Text {
        font.family: Theme.sans
        font.pixelSize: Theme.labelSize
        color: Theme.mutedText
        wrapMode: Text.WordWrap
    }

    Text {
        visible: !screen.results.hasLookup
        anchors.centerIn: parent
        text: "No lookup is open. Paste a listing link on the Lookups screen."
        font.family: Theme.sans
        font.pixelSize: Theme.bodySize
        color: Theme.mutedText
    }

    ScrollView {
        id: scroll
        visible: screen.results.hasLookup
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth - 56
            x: 28
            spacing: Theme.gap

            Item { Layout.preferredHeight: 4 }

            Muted {
                Layout.fillWidth: true
                font.family: Theme.mono
                text: screen.results.statusText
                color: screen.results.statusKind === "complete" ? Theme.mutedText : Theme.warnLabel
            }
            Muted {
                Layout.fillWidth: true
                visible: screen.results.storeError.length > 0
                text: screen.results.storeError
                color: Theme.warnLabel
            }

            // Identity bar.
            Card {
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.gap
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: "Identified as <b>" + screen.results.identityName.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>"
                                  + (screen.results.productName.length > 0
                                     ? " - " + screen.results.productName.replace(/&/g, "&amp;").replace(/</g, "&lt;") : "")
                            wrapMode: Text.WordWrap
                            font.family: Theme.sans
                            font.pixelSize: Theme.sectionSize
                            color: Theme.text
                        }
                        Muted {
                            Layout.fillWidth: true
                            text: screen.results.identitySource + "  ·  searches: " + screen.results.searches
                        }
                        // Owner case 11: the identity does not match the listing's
                        // own title; no numbers were computed from it.
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: screen.results.identityWarning
                            wrapMode: Text.WordWrap
                            font.family: Theme.sans
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            color: Theme.warnLabel
                        }
                        Repeater {
                            model: screen.results.identityNotes
                            Muted {
                                required property string modelData
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.secondaryText
                            }
                        }
                    }
                    AppButton {
                        text: "Wrong item? Correct it"
                        enabled: !screen.backend.session.running && screen.results.itemId.length > 0
                        onClicked: editor.openWith(screen.results.brand, screen.results.model, screen.results.productName)
                    }
                }
            }

            // Listing card.
            Card {
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    spacing: 24
                    Rectangle {
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 150
                        radius: Theme.controlRadius
                        color: Theme.raised
                        border.color: Theme.border
                        Image {
                            anchors.fill: parent
                            anchors.margins: 4
                            source: screen.results.photoSource
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            cache: false
                            Accessible.name: "Photo of the listing"
                        }
                        Muted {
                            visible: screen.results.photoSource.length === 0
                            anchors.centerIn: parent
                            text: "no photo"
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "THIS LISTING - item " + screen.results.itemId
                            font.family: Theme.mono
                            font.pixelSize: Theme.labelSize
                            font.weight: Font.Medium
                            color: Theme.accent
                        }
                        Text {
                            Layout.fillWidth: true
                            text: screen.results.title.length > 0 ? screen.results.title : "(the listing was not read)"
                            wrapMode: Text.WordWrap
                            font.family: Theme.sans
                            font.pixelSize: Theme.sectionSize
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Muted {
                                Layout.fillWidth: screen.results.seller.length === 0
                                text: "eBay condition: " + (screen.results.condition.length > 0 ? screen.results.condition : "-")
                                      + "  ·  seller: " + (screen.results.seller.length > 0 ? "" : "-")
                                color: Theme.secondaryText
                            }
                            // The seller's name opens the seller panel.
                            LinkText {
                                Layout.fillWidth: true
                                visible: screen.results.seller.length > 0
                                text: screen.results.seller
                                elide: Text.ElideRight
                                font.pixelSize: Theme.labelSize
                                Accessible.name: "Seller " + screen.results.seller + ", open the seller panel"
                                onActivated: screen.openSeller()
                            }
                        }
                        // Format (owner decision 2026-10-19): Buy It Now, Best
                        // Offer, or the auction with its bids and end.
                        Text {
                            Layout.fillWidth: true
                            text: screen.results.formatText
                            wrapMode: Text.WordWrap
                            font.family: Theme.mono
                            font.pixelSize: Theme.labelSize + 1
                            color: Theme.text
                        }
                        // The auction's countdown, from the end read on the
                        // page (never by loading eBay again).
                        Text {
                            id: countdown
                            property string remaining: "-"
                            visible: screen.results.isAuction && screen.results.auctionEndMs >= 0
                            text: countdown.remaining
                            font.family: Theme.mono
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.Medium
                            color: countdown.remaining === "ENDED" ? Theme.warnLabel : Theme.accent
                            Accessible.name: "Time left: " + countdown.remaining
                            Timer {
                                interval: 1000
                                repeat: true
                                triggeredOnStart: true
                                running: countdown.visible
                                onTriggered: countdown.remaining = screen.results.timeLeft(screen.results.auctionEndMs,
                                                                                      screen.results.auctionEndSeconds)
                            }
                        }
                        LinkText {
                            visible: screen.results.itemId.length > 0
                            text: "Open on eBay"
                            onActivated: screen.backend.openOnEbay(screen.results.itemId)
                        }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        spacing: 2
                        Muted {
                            Layout.alignment: Qt.AlignRight
                            text: "price + shipping ="
                        }
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: screen.results.priceText + " + " + screen.results.shippingText
                            font.family: Theme.mono
                            font.pixelSize: Theme.bodySize
                            color: Theme.secondaryText
                        }
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: screen.results.totalText
                            font.family: Theme.mono
                            font.pixelSize: Theme.bigTotalSize
                            font.weight: Font.Medium
                            color: Theme.text
                            Accessible.name: "Total " + text
                        }
                    }
                }
            }

            // Flag cards, only the ones that apply.
            RowLayout {
                Layout.fillWidth: true
                visible: screen.results.brokenFlag || screen.results.isAuction || screen.results.shippingFlag
                         || screen.results.cheaperFlag || screen.results.markedFlag || screen.results.ratingFlag
                spacing: Theme.gap
                FlagCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.isAuction
                    kind: "warn"
                    label: screen.results.auctionLabel
                    lines: screen.results.auctionLines
                }
                FlagCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.brokenFlag
                    kind: "danger"
                    label: "LIKELY BROKEN"
                    lines: [screen.results.brokenLine]
                }
                FlagCard {
                    objectName: "markedFlag"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.markedFlag
                    kind: "danger"
                    label: "YOU MARKED THIS SELLER"
                    lines: screen.results.markedLines
                    linkText: "Open seller"
                    onLinkActivated: screen.openSeller()
                }
                FlagCard {
                    objectName: "ratingFlag"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.ratingFlag
                    kind: "danger"
                    label: "SELLER RATING"
                    lines: [screen.results.ratingLine]
                }
                FlagCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.shippingFlag
                    kind: "warn"
                    label: "SHIPPING"
                    lines: screen.results.shippingLines
                }
                FlagCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: screen.results.cheaperFlag
                    kind: "info"
                    label: "CHEAPER RIGHT NOW"
                    lines: [screen.results.cheaperLine]
                    linkText: "Open the cheapest on eBay"
                    onLinkActivated: screen.backend.openOnEbay(screen.results.cheapestItemId)
                }
            }

            // Plugins (DESIGN 5): what went wrong with one (it was disabled),
            // and the flaggers' warnings and notes, each with its plugin's name.
            Repeater {
                model: screen.results.pluginNotices
                Muted {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "Plugin: " + modelData
                    color: Theme.warnLabel
                }
            }
            Flow {
                Layout.fillWidth: true
                visible: screen.results.pluginFlags.length > 0
                spacing: Theme.gap
                Repeater {
                    model: screen.results.pluginFlags
                    FlagCard {
                        required property var modelData
                        width: Math.min(420, parent.width)
                        kind: modelData.kind
                        label: modelData.plugin.toUpperCase()
                        lines: [modelData.text]
                    }
                }
            }

            // Every search found nothing: say so instead of empty tables.
            FlagCard {
                Layout.fillWidth: true
                visible: screen.results.emptyMessage.length > 0
                kind: "warn"
                label: "NOTHING FOUND"
                lines: [screen.results.emptyMessage]
            }

            // What the same item sold for: one bordered box per source, each
            // number in a tile of its own, the bar in its own box (owner
            // 2026-10-20 "make it readable"; ui-spec 2).
            ColumnLayout {
                objectName: "soldFor"
                Layout.fillWidth: true
                visible: screen.results.emptyMessage.length === 0
                spacing: Theme.unit
                Text {
                    text: "What the same item really sold for"
                    font.family: Theme.sans
                    font.pixelSize: Theme.sectionSize
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                StatBox {
                    objectName: "soldBox"
                    heading: screen.results.soldHeading
                    footnotes: screen.results.lookupNotes
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.unit
                        NumberTile { label: "Median sold"; value: screen.results.medianText }
                        NumberTile { label: "Middle half"; value: screen.results.middleText }
                        NumberTile { label: "Lowest / highest"; value: screen.results.lowHighText }
                        NumberTile {
                            label: "Sales counted"
                            value: screen.results.soldCountText
                            detail: screen.results.soldConfidenceText
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: screen.results.soldBestOfferNote
                        wrapMode: Text.WordWrap
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        color: Theme.secondaryText
                    }
                }
                StatBox {
                    objectName: "researchBox"
                    heading: screen.results.researchHeading
                    footnotes: [screen.results.researchFootnote]
                    RowLayout {
                        Layout.fillWidth: true
                        visible: screen.results.researchStatus.length === 0
                        spacing: Theme.unit
                        NumberTile { label: "Median"; value: screen.results.researchMedianText }
                        NumberTile { label: "Middle half"; value: screen.results.researchMiddleText }
                        NumberTile { label: "Lowest / highest"; value: screen.results.researchLowHighText }
                        NumberTile {
                            label: "Listings counted"
                            value: screen.results.researchCountText
                            detail: screen.results.researchConfidenceText
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: screen.results.researchStatus
                        wrapMode: Text.WordWrap
                        font.family: Theme.sans
                        font.pixelSize: Theme.bodySize
                        color: Theme.secondaryText
                    }
                }
                StatBox {
                    objectName: "forSaleBox"
                    heading: "For sale now"
                    footnotes: [screen.results.forSaleFootnote]
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.unit
                        NumberTile { label: "Buy It Now listings"; value: screen.results.binCountText }
                        NumberTile {
                            label: "Cheapest Buy It Now"
                            value: screen.results.cheapestBinText
                            linkText: screen.results.cheapestItemId.length > 0 ? "View" : ""
                            onLinkActivated: screen.backend.openOnEbay(screen.results.cheapestItemId)
                        }
                        NumberTile { label: "Auctions running"; value: screen.results.auctionsCountText }
                        NumberTile {
                            label: "Lowest current bid"
                            value: screen.results.lowestBidText
                            detail: screen.results.auctionsCountText !== "0" ? "not a final price" : ""
                        }
                    }
                }
                PriceBarBox {
                    objectName: "barBox"
                    visible: screen.results.hasSoldNumbers || screen.results.hasResearchNumbers
                    results: screen.results
                }
                // Valuator plugins: figures next to the app's own, never in
                // their place.
                StatBox {
                    visible: screen.results.pluginValues.length > 0
                    heading: "Plugin figures"
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.unit
                        Repeater {
                            model: screen.results.pluginValues
                            NumberTile {
                                required property var modelData
                                width: 260
                                label: modelData.label + " (plugin: " + modelData.plugin + ")"
                                value: modelData.value
                                detail: modelData.note
                            }
                        }
                    }
                }
            }

            // Save this lookup: as JSON (built in), or with an exporter plugin.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                AppButton {
                    text: "Save as JSON"
                    Accessible.name: "Save this lookup as a JSON file in Downloads"
                    onClicked: exportResult.show(screen.results.saveJson())
                }
                Repeater {
                    model: screen.results.exporters
                    AppButton {
                        required property var modelData
                        text: "Save with " + modelData.name
                        onClicked: exportResult.show(screen.results.exportWith(modelData.id))
                    }
                }
                Muted {
                    id: exportResult
                    property string path
                    property bool ok: true
                    function show(reply) {
                        ok = reply.ok
                        path = reply.ok ? reply.path : ""
                        text = reply.message
                    }
                    Layout.fillWidth: true
                    visible: text.length > 0
                    color: ok ? Theme.secondaryText : Theme.warnLabel
                }
                LinkText {
                    visible: exportResult.path.length > 0
                    text: "Show in folder"
                    onActivated: screen.backend.showFolderOf(exportResult.path)
                }
            }

            // Tabs.
            RowLayout {
                visible: screen.results.emptyMessage.length === 0
                spacing: 8
                Repeater {
                    model: [
                        "Same item (" + screen.results.sameItem.count + ")",
                        "Not this item (" + screen.results.notThisItem.count + ")"
                    ]
                    Button {
                        id: tabButton
                        required property string modelData
                        required property int index
                        text: modelData
                        implicitHeight: Theme.target
                        leftPadding: 16
                        rightPadding: 16
                        Accessible.role: Accessible.PageTab
                        contentItem: Text {
                            text: tabButton.text
                            font.family: Theme.sans
                            font.pixelSize: Theme.bodySize
                            font.weight: screen.tab === tabButton.index ? Font.DemiBold : Font.Normal
                            color: screen.tab === tabButton.index ? Theme.text : Theme.secondaryText
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: screen.tab === tabButton.index ? Theme.raised : "transparent"
                            border.color: tabButton.visualFocus ? Theme.accent : "transparent"
                            Rectangle {
                                visible: screen.tab === tabButton.index
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 16
                                height: 2
                                color: Theme.accent
                            }
                        }
                        onClicked: screen.tab = index
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                visible: screen.results.emptyMessage.length === 0
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowsTable {
                        Layout.fillWidth: true
                        visible: screen.tab === 0
                        rows: screen.results.sameItem
                        onRowToggled: (row, included) => screen.results.setIncluded(row, included)
                        onOpenRequested: (itemId) => screen.backend.openOnEbay(itemId)
                        onSellerClicked: (seller, percent, count) => sellerPanel.show(seller, seller, percent, count)
                    }
                    RowsTable {
                        Layout.fillWidth: true
                        visible: screen.tab === 1
                        rows: screen.results.notThisItem
                        showReasons: true
                        onRowToggled: (row, included) => screen.results.setIncluded(row, included)
                        onOpenRequested: (itemId) => screen.backend.openOnEbay(itemId)
                        onSellerClicked: (seller, percent, count) => sellerPanel.show(seller, seller, percent, count)
                    }
                    Muted {
                        Layout.fillWidth: true
                        text: "Ticked rows are counted. Same-item rows start ticked; tick a row in \"Not this item\" if it "
                              + "is the same thing after all. Every number above is computed again at once and your "
                              + "choice is saved with this lookup."
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }
        }
    }

    IdentityEditor {
        id: editor
        onSaveRequested: (brand, model, productName) => {
            if (screen.backend.correctIdentity(brand, model, productName))
                editor.close()
            else
                editor.error = screen.backend.session.errorText
        }
    }

    SellerPanel {
        id: sellerPanel
        backend: screen.backend
    }
}
