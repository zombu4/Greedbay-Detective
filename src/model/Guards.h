#pragma once

#include "lookup/Identity.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupReport.h"
#include "lookup/MatchWords.h"
#include "lookup/Records.h"
#include "model/Prompts.h"

#include <QString>

namespace app::model {

// The guards of the local model's answers (DESIGN 3.6, item 5). They run
// after the model and always win: a guard only refuses an answer, moves a
// row to "to check", or makes the app ask the user. Nothing here makes an
// answer more certain than the rules' own.

struct Verdict {
    bool accepted = false;
    QString guard; // "G4" when refused
    QString why;   // shown in the lookup's note
    QString model; // accepted: the model as the title writes it
};

// The identity answer for a listing (G2-G6; G1 is parseIdentity()):
//   G5 the brand is no notBrands value, title opener or junk brand word
//      (compared by its title words, so "Unbranded." is "Unbranded"); when
//      the listing's own "Brand" specific names a maker, the answer names
//      that maker; else it is a known brand or the title's first maker word;
//   G2 the brand (or a name of the known brand) stands as whole words in the
//      title or the item specifics;
//   G3 the model stands in the TITLE (one word or 2-4 neighbouring words
//      written together, compared as normalized codes) and is no value of a
//      productCodeLabels specific;
//   G4 there it is no specification, and either model-like or a known model
//      of the brand;
//   G6 a digits-only model stands right after a brand name and is no year
//      (1900-2099) or count (1-2 digits), unless it is a known model.
// An accepted answer's model is replaced by the title's own spelling.
Verdict checkIdentity(const IdentityAnswer &answer, const lookup::ItemRecord &item, const lookup::KnownModels &known,
                      const lookup::MatchWords &words);

// G7: the model is asked about the identity only when the rules' identity
// came from the title or the specifics, or there is none (never on the
// user's, a saved confirmation or a known model).
bool mayAskIdentity(const lookup::Identity &rules);

// True when the answer names the rules' brand (a name of the same known
// brand, or the same name) and model (the same normalized code or a family
// member).
bool sameIdentity(const IdentityAnswer &answer, const lookup::Identity &rules, const lookup::KnownModels &known);

// What the model's category does to one row (G8-G10):
//   rows the rules put in "same item": the same answer changes nothing; any
//     other answer moves the row to "to check" with both reasons (G10) -
//     except the listing's own row, which never moves (G8);
//   rows in "to check": the answer is added as a suggestion; the row stays
//     there; a "same item" suggestion for a system's title with a media word
//     (game, cartridge, ...) is refused (G9);
//   rows in any other bucket are never changed (they are not sent).
// "unknown" changes nothing.
struct SortEffect {
    enum class Kind { None, ToCheck, Suggest, Refused };
    Kind kind = Kind::None;
    QString reason; // added to the row's reasons
    QString guard;  // for Refused (and the G8 case)
};
SortEffect sortEffect(lookup::Bucket rulesBucket, bool ownListing, const QString &category, const QString &title,
                      const lookup::Identity &identity, const lookup::MatchWords &words);

// Whether the model is asked about a row at all (G9: only "same item" and
// "to check" rows).
bool mayAskSort(lookup::Bucket rulesBucket);

} // namespace app::model
