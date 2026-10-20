#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::lookup {

// The word lists of the matcher (data/match-words.json, built in as
// :/lookup/match-words.json). Every list is data, not code: what each one
// does is described in Matcher.h.
struct MatchWords {
    QStringList specUnits;        // a number with one of these (glued or as the next word) is a specification
    QStringList specPrefixes;     // <prefix>-<number> is a specification (DC-600)
    QStringList specUnitsGlued;   // units only glued to the number ("16GB", "2-PORT"), never as a word of their own
    QStringList specUnitsInPairs; // units only inside a range or a slash pair ("0-15A", "8V/3A", "2M/6M")
    QStringList currentUnits;     // a number with one of these is a specification when the title states a voltage ("25V 30A")
    QStringList voltageUnits;
    QStringList notModels;        // regular expressions: model-like words that are no model
    QStringList optionWords;      // a code right after one of these is an option ("Opt 1E5")
    QStringList modelWords;       // "<brand> model <code>": the code counts as right after the brand
    QStringList notCodePrefixes;  // short words never joined with the next number into a model ("No 5")
    QStringList tubeTypes;        // vacuum tube types: not another model of the item
    QStringList notBrands;        // Brand values that name no brand
    QStringList lot;
    QStringList documentation;
    QStringList partFits;     // a part on its own
    QStringList partWords;    // a part when the title names another model too
    QStringList partOnly;     // a part on its own unless it follows a with-item word ("with front panel")
    QStringList partPatterns; // regular expressions: chips, revisions, board locations (8580R5, Z80B, U18)
    QStringList kitWords;     // kit, set: decides nothing alone (owner case 9)
    QStringList kitParts;     // with a kit word: a parts or repair kit
    QStringList kitFor;       // "kit for": a kit for something
    QStringList completeKit;  // with a kit word: the brand's complete kit, the item
    QStringList likelyBroken;    // title words: likely broken (warning only)
    QStringList partsConditions; // eBay conditions: likely broken
    QStringList accessory;
    QStringList notAccessory; // phrases with an accessory word that are the item's own ("original case")
    QStringList withItem;     // an accessory word after one of these is part of the item
    QStringList companion;    // "for": "<other model> ... for <model>"

    // Only for models of these categories (computers, consoles, handhelds):
    QStringList systemCategories;
    QStringList itemNouns;       // the system itself is named: media and accessories come with it
    QStringList media;           // games, cartridges, disks: software for the system
    QStringList systemAccessory; // controllers, power supplies, cables
    QStringList reissue;         // re-issues (Classic Edition, Mini, Flashback): another model
    QStringList merchandise;     // posters, stickers, mugs: not the item

    // Normalized tubeTypes, for lookups.
    QSet<QString> tubeCodes;

    // std::nullopt (and *error) when `json` is not the format-1 object with
    // every list present as an array of non-empty strings.
    static std::optional<MatchWords> fromJson(const QByteArray &json, QString *error);
    // The built-in lists.
    static std::optional<MatchWords> builtIn(QString *error);
};

} // namespace app::lookup
