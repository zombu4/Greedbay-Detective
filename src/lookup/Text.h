#pragma once

#include <QString>
#include <QStringList>

namespace app::lookup {

// Text helpers shared by the identity, the matcher and the known-models
// data.

// A model code in one form for comparing: upper case, without "-", ".", "/"
// and spaces. "AA-100", "AA100", "aa 100" -> "AA100".
QString normalizeCode(const QString &code);

// The words of a title or field, in order. Split at white space and at
// , ; ( ) [ ] ! ? " ' # * | : and typographic quotes; NOT at "-", "/" or
// ".", so a hyphen- or slash-joined code ("15208-AA100", "SP-PR570-BML",
// "M7X/B2") stays one word (owner rule 10 and case 3). Leading and trailing
// "-", "." and "/" are removed from each word, except from "p/n"; "w/" is
// always a word of its own, also when glued to the next ("w/Leads" -> "w/",
// "Leads").
QStringList titleWords(const QString &text);

// The same words, lower case, with "/"-joined words also split at "/"
// ("parts/repair" -> "parts", "repair", "HP/Agilent" -> "hp", "agilent"),
// except "w/" and "p/n"; for matching the word lists and brand names.
QStringList listWords(const QString &text);

// True when `word` has at least one letter and one digit.
bool hasLetterAndDigit(const QString &word);

// The positions in `words` where the phrase starts (its words compared
// without case); a phrase is split at spaces.
QList<qsizetype> phrasePositions(const QStringList &words, const QString &phrase);

} // namespace app::lookup
