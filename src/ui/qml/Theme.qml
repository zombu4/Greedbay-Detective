// The design tokens of the owner-approved mockups (ui-spec "Look"). Dark
// theme only for now; a light theme later uses the same names.
pragma Singleton
import QtQuick

QtObject {
    readonly property color ground: "#16181b"
    readonly property color surface: "#1f2226"
    readonly property color raised: "#23272c"
    readonly property color border: "#2a2e33"
    readonly property color inputBorder: "#3a3f45"
    readonly property color text: "#e8e6e1"
    readonly property color secondaryText: "#c4c9cf"
    readonly property color mutedText: "#a7adb4"
    readonly property color accent: "#e8a33d"
    readonly property color accentText: "#16181b"
    readonly property color warnBackground: "#2b2113"
    readonly property color warnBorder: "#5c3d14"
    readonly property color warnLabel: "#ffc77a"
    readonly property color infoBackground: "#15253a"
    readonly property color infoBorder: "#2d4a70"
    readonly property color infoLabel: "#b9d3ff"
    readonly property color infoMarker: "#8fb8ff"
    readonly property color link: "#8fb8ff"
    readonly property color warnBadgeBackground: "#4a2f12"
    readonly property color warnBadgeText: "#ffc77a"
    readonly property color infoBadgeBackground: "#1c3350"
    readonly property color infoBadgeText: "#b9d3ff"
    readonly property color band: "#4a5563"

    // Bundled IBM Plex (src/ui/Fonts.cpp): Sans for text, Mono for every
    // price, number, date and item id.
    readonly property string sans: "IBM Plex Sans"
    readonly property string mono: "IBM Plex Mono"

    // 8 px grid.
    readonly property int unit: 8
    readonly property int cardRadius: 14
    readonly property int controlRadius: 8
    readonly property int cardPadding: 20
    readonly property int gap: 16
    // Smallest click target.
    readonly property int target: 44

    readonly property int titleSize: 22
    readonly property int headingSize: 24
    readonly property int sectionSize: 18
    readonly property int bodySize: 15
    readonly property int labelSize: 13
    readonly property int bigTotalSize: 36
    readonly property int keyNumberSize: 24
}
