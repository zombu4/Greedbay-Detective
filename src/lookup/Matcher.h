#pragma once

#include "lookup/Identity.h"
#include "lookup/MatchWords.h"

#include <QString>
#include <QStringList>

namespace app::lookup {

// What one result row is, judged from its title against the listing's
// identity, with the reasons shown to the user
// (owner rules 3, 4, 10, 13, 14, 17, 19, 28; DESIGN 3.3). All words come from
// MatchWords.
//
// Model found: one title word, or 2-3 neighbouring words written together,
// whose normalized code equals the model's or a family member's (AA-100 =
// AA100 = AA 100). A code inside a longer word ("15208-AA100",
// "SP-PR570-BML", "VG91-5040") or a longer model with the same start
// (PR570 for PR57) is not the model. Brand found: one of the brand's names
// as whole words, also inside a slash-joined word ("HP/Agilent").
//
// Bucket, first that applies:
//   UnknownItem    no title (rule 19)
//   NoMatch        the model is not in the title
//   ToCheck        the model is there but no brand name (owner case 7 had a
//                  real sale titled without its brand), or a digits-only model
//                  that does not directly follow the brand name (a year?);
//                  never counted unless the user ticks it
//   Lot            a lot word
//   Documentation  a documentation word not following a with-item word
//                  ("with manual" belongs to the item)
//   Companion      "<other model> ... for <model>" (rule 13)
//   Accessory      "<accessory word> ... for <model>"
//   PartFits       any other "... for <model>"; a part-fits word; a part
//                  word with another model code in the title; or two or
//                  more other model codes in the title (rules 4, 14)
//   Accessory      an accessory word that does not follow a with-item word
//                  ("w/ Leads" belongs to the item, case 1)
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

QString bucketName(Bucket bucket);

} // namespace app::lookup
