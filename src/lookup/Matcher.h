#pragma once

#include "lookup/Identity.h"
#include "lookup/MatchWords.h"

#include <QString>
#include <QStringList>

namespace app::lookup {

// What one result row is, judged from its title (and eBay's condition text)
// against the listing's identity, with the reasons shown to the user
// (owner rules 3, 4, 10, 13, 14, 17, 19, 28; DESIGN 3.3). All words come from
// MatchWords.
//
// Model found: one title word, or 2-3 neighbouring words written together,
// whose normalized code equals the model's or a family member's (AA-100 =
// AA100 = AA 100). A code inside a longer word ("15208-AA100",
// "SP-PR570-BML", "VG91-5040") or a longer model with the same start
// (PR570 for PR57) is not the model. Brand found: one of the brand's names
// as whole words.
//
// Bucket, first that applies:
//   UnknownItem    no title (rule 19)
//   NoMatch        the model or the brand is not in the title
//   Lot            a lot word
//   Documentation  a documentation word
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
enum class Bucket { SameItem, Documentation, PartFits, Companion, Accessory, Lot, UnknownItem, NoMatch };

// Condition scale (rule 18), from title words first - the strongest found
// counts: NonWorking > Untested > New > Working; a longer phrase wins over a
// shorter one inside it ("not working" over "working"). Without such words,
// eBay's condition text: parts -> NonWorking, new -> New, used ->
// NotStated. Otherwise NotStated.
enum class Condition { New, Working, NotStated, Untested, NonWorking };

struct Match {
    Bucket bucket = Bucket::NoMatch;
    Condition condition = Condition::NotStated;
    QString conditionWord; // the word that decided it, or eBay's text
    QStringList reasons;
};

Match matchRow(const QString &title, const QString &ebayCondition, const Identity &identity, const MatchWords &words);

// The condition alone (for the listing itself).
Match conditionOf(const QString &title, const QString &ebayCondition, const MatchWords &words);

QString bucketName(Bucket bucket);
QString conditionName(Condition condition);

} // namespace app::lookup
