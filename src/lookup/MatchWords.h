#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::lookup {

// The word lists of the matcher (data/match-words.json, built in as
// :/lookup/match-words.json). Every list is data, not code: what each one
// does is described in Matcher.h; where its words come from is in the file.
struct MatchWords {
    QStringList specUnits;    // a number with one of these is a specification
    QStringList specPrefixes; // <prefix>-<number> is a specification (DC-600)
    QStringList notModels;    // regular expressions: model-like words that are no model
    QStringList notBrands;    // Brand values that name no brand
    QStringList lot;
    QStringList documentation;
    QStringList partFits;     // a part on its own
    QStringList partWords;    // a part when the title names another model too
    QStringList accessory;
    QStringList withItem;     // an accessory word after one of these is part of the item
    QStringList companion;    // "for": "<other model> ... for <model>"
    QStringList nonWorking;
    QStringList untested;
    QStringList newWords;
    QStringList working;
    QStringList ebayNew;      // eBay condition texts that mean new
    QStringList ebayUsed;     // eBay condition texts that mean used, condition not stated
    QStringList ebayNonWorking; // eBay condition texts that mean parts / not working

    // std::nullopt (and *error) when `json` is not the format-1 object with
    // every list present as an array of non-empty strings.
    static std::optional<MatchWords> fromJson(const QByteArray &json, QString *error);
    // The built-in lists.
    static std::optional<MatchWords> builtIn(QString *error);
};

} // namespace app::lookup
