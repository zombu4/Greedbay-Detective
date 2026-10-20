#pragma once

#include "lookup/Identity.h"
#include "lookup/MatchWords.h"

#include <QString>
#include <QStringList>

namespace app::lookup {

// What one result row is, judged from its title against the listing's
// identity, with the reasons shown to the user
// (owner rules 3, 4, 10, 13, 14, 17, 19, 28; DESIGN 3.3; brand-scan cases
// S1-S18 and retro-scan cases R1-R13). All words come from MatchWords.
//
// Model found: one title word, or 2-4 neighbouring words written together,
// whose normalized code equals the model's or a family member's (AA-100 =
// AA100 = AA 100). A code inside a longer word ("15208-AA100",
// "SP-PR570-BML", "VG91-5040") or a longer model with the same start
// (PR570 for PR57; a longer known model of the brand, "Game Boy Color" for
// "Game Boy") is not the model. Brand found: one of the brand's names
// as whole words, also inside a slash-joined word ("HP/Agilent").
//
// The word lists are looked for in the title without the brand names, the
// model and the product name, so they never fire inside them ("Heath Kit",
// "Game Boy", the product name "Test Set"). "System" lists apply only to
// models of a systemCategories category (computers, consoles, handhelds).
//
// Bucket, first that applies:
//   UnknownItem    no title (rule 19)
//   NoMatch        the model is not in the title; a digits-only model
//                  without any brand name (a year or a number; owner decision
//                  2026-10-19); a plain-word model without its brand
//                  ("Camputers Lynx", R7)
//   ToCheck        the model is there but no brand name (owner case 7 had a
//                  real sale titled without its brand); never counted unless
//                  the user ticks it
//   Documentation  a digits-only model that does not directly follow a brand
//                  name or the product name (or "<name> model"), in a
//                  documentation title
//   ToCheck        such a model in any other title (a year?)
//   NoMatch        a system re-issue ("Classic Edition", "Mini", R4) or
//                  merchandise (poster, mug, R12)
//   Lot            a lot word ("lot of", "station package", S16)
//   Documentation  a documentation word not following a with-item word
//                  ("with manual" belongs to the item; S7, R3)
//   PartFits       a part-fits word ("compatible"); a part-only word not
//                  following a with-item word ("front panel", "box only";
//                  S6, S11, R12); a chip, revision or board location
//                  (partPatterns) unless a system title names the system
//                  itself (R10)
//   PartFits       a kit word (kit, set) not following a with-item word
//                  (S10) with a kit-parts word (recap, cap, capacitors,
//                  diodes, repair, upgrade, ...) or "kit for" (owner case 9)
//   ToCheck        a kit word without those and without a complete-kit word
//                  (unbuilt, complete): a complete kit or parts for it?
//   (a kit word with a complete-kit word goes on: the brand's own kit of
//   the device is the item)
//   Companion      "<other model> ... for <model>" (rule 13)
//   Accessory      "<accessory word> ... for <model>"
//   PartFits       any other "... for <model>"
//   Companion      a system title with a media word (game, cartridge, CIB)
//                  that does not name the system itself and does not follow
//                  a with-item word (R1, R13)
//   Accessory      a system title that names another system (R6)
//   PartFits       a part word with another model code in the title; two
//                  or more other model codes in the title (rules 4, 14; tube
//                  types and a seller's code at the end are no other model,
//                  S3, S4)
//   Accessory      an accessory word (for systems also controllers, power
//                  supplies, cables, unless the title names the system
//                  itself; R2) that does not follow a with-item word ("w/
//                  Leads", case 1), is not inside a notAccessory phrase
//                  ("original case", S12) and is not one of the identity's
//                  ownWords (S13)
//   SameItem       otherwise
// "for <model>" means a companion word directly followed, within three
// words (a brand name may stand between), by the model.
enum class Bucket { SameItem, Documentation, PartFits, Companion, Accessory, Lot, UnknownItem, ToCheck, NoMatch };

// The app does not grade condition (owner decision 2026-10-19, replacing
// rules 5, 15, 18 and 22): matching only decides whether a row is the same
// item; eBay's own condition text is shown as it is.
struct Match {
    Bucket bucket = Bucket::NoMatch;
    QStringList reasons;
};

Match matchRow(const QString &title, const Identity &identity, const MatchWords &words);

// The accessory words (for systems also the system accessory words) that
// the pasted listing's `title` uses for the item itself: not after a
// with-item word, not inside a notAccessory phrase ("HP N2894A passive
// probe" -> "probe"). Stored in Identity::ownWords before rows are matched.
QStringList itemOwnWords(const QString &title, const Identity &identity, const MatchWords &words);

QString bucketName(Bucket bucket);

// Owner decision 2026-10-19 ("red flag likely broken"): a WARNING, not
// grading - the numbers do not change. The reason when eBay's condition is
// one of partsConditions ("eBay says 'For parts or not working'") or the
// title holds a likelyBroken phrase ("the title says 'untested'"); empty
// otherwise. Nothing else is guessed (no flag from returns or a low price).
QString likelyBrokenReason(const QString &title, const QString &ebayCondition, const MatchWords &words);

} // namespace app::lookup
