#pragma once

#include "lookup/Records.h"

#include <QString>
#include <QStringList>

namespace app::lookup {

// What a listing is, for building searches (DESIGN 3.2, PLAN 5 step 4), in
// the rough form of phase 3b; the matching rules and word lists of phase 4
// are not part of it.
//
// brand: the item specific "Brand" exactly as the seller entered it (no
//   dictionary yet: the brand dictionary ships with the phase-4 matcher).
// modelTokens: canonical model tokens, first the ones from the item specifics
//   "Model" and "MPN" (in that order), then from the title. A model token is
//   a word with at least one digit; from the title it must also have a
//   letter, because bare numbers there are mostly quantities and years.
//   Words are split at white space and at , ; / ( ) [ ] | + : & ! ? " and
//   quotes. A number directly followed by a single capital letter as its own
//   word ("87 V") is joined to it first ("87V"). Canonical form: upper case,
//   hyphens and dots removed ("87-V" -> "87V"). Accessory words never have a
//   digit, so they are never model tokens.
struct Identity {
    QString brand;
    QStringList modelTokens;
};

Identity identify(const ItemRecord &item);

// The canonical form of one model token: upper case, without "-" and ".".
QString canonicalModel(const QString &token);

// Model tokens of `text` in order, canonical and without repeats.
// `requireLetter`: skip tokens that are digits only.
QStringList modelTokens(const QString &text, bool requireLetter);

// 1 to 3 search queries (DESIGN 3.2), without repeats (case ignored):
//   1. brand + first model token   (when both exist)
//   2. first model token alone
//   3. brand + second model token  (when a second one exists; the brand is
//      left out when there is none)
// With no model token at all: the title alone, for the user to edit.
QStringList buildQueries(const Identity &identity, const QString &title);

} // namespace app::lookup
