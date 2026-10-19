#pragma once

#include "lookup/KnownModels.h"
#include "lookup/MatchWords.h"
#include "lookup/Records.h"

#include <QString>
#include <QStringList>

namespace app::lookup {

// What a listing is (owner rules 1, 7, 21-28; DESIGN 3.2).
//
// Brand: the item specific "Brand" when it names a known brand (any of its
// names) or, failing that, contains one as whole words; an unknown "Brand"
// value is taken as written (a new brand with that one name), unless it is
// one of the notBrands values ("Unbranded"). Without a usable "Brand"
// specific, the known brand whose name appears first in the title.
//
// Model: first a known model (or family member) of that brand appearing as a
// whole word, or 2-3 neighbouring words written together, in the "Model" or
// "MPN" specific or the title. Otherwise the model-like words
// (isModelLike: a letter and a digit, numbers-only never, no specification,
// not a count or version) that are no brand name, in upper
// case with its hyphens kept as written (AA-100, IM-13, LC53, 8904A). These
// words are never joined ("53 Z Meter" never gives 53Z); only a known model
// is looked for across neighbouring words. The model is confident
// when the "Model" specific has exactly one such word, else the "MPN"
// specific, else the title (each: exactly one distinct normalized code).
// Anything else - no brand, no model word, or several - is not confident:
// the app asks the user (rule 26) and suggests the candidates and the known
// models of the brand.
struct Identity {
    QString brandId;           // empty when no brand is known
    QString brandName;         // shown: the name found, or the first name
    QStringList matchNames;    // every name of the brand
    QStringList searchNames;   // the names used in queries
    QString model;             // manufacturer's spelling; empty when unknown
    QStringList family;        // codes that count as the same item
    QString productName;
    QStringList notes;         // known-issue notes of the model
    bool confident = false;
    // Where it came from: "saved" (the user confirmed it for this listing),
    // "user", "known model", "item specifics", "title".
    QString source;
    QString reason;            // why it is not confident
    QStringList candidates;    // suggestions for the user

    bool canSearch() const { return confident && !searchNames.isEmpty() && !model.isEmpty(); }
};

// The identity of a listing. A confirmed identity saved for its item number
// comes first.
Identity identify(const ItemRecord &item, const KnownModels &known, const MatchWords &words);

// The identity the user entered (rule 7): a known brand by any of its names,
// else a new brand with this one name; the known model's spelling, family,
// product name and notes when the brand has that model.
Identity identityFromUser(const ConfirmedIdentity &entered, const KnownModels &known, const QString &source);

// One query per search name of the brand: "<name> <model>" (rules 21, 23).
// No model-only queries and no spelling variants. Empty when the identity
// cannot be searched.
QStringList buildQueries(const Identity &identity);

// True when words[index] is a specification, not a model (rule 24): a
// number with one of the spec units glued on ("1500V", "50kv", "600kHz",
// "45%"), a range of such numbers ("10kHz-1000MHz", "0-1500V"), a spec
// prefix with a number and an optional unit ("DC-600", "DC-600kHz"), or a word ending in a digit that is
// followed by a unit word ("600 KHZ", "15 MEG", "4 channels").
bool isSpecification(const QStringList &words, qsizetype index, const MatchWords &matchWords);

// True when words[index] can be a model code: a letter and a digit, no
// specification, and none of the notModels patterns (1960s, 3rd, 4pcs, Mk2).
bool isModelLike(const QStringList &words, qsizetype index, const MatchWords &matchWords);

// The model-like words of `text` (see above), upper case, without repeats
// by normalized code; `brand` names are skipped.
QStringList modelCandidates(const QString &text, const QStringList &brandNames, const MatchWords &matchWords);

} // namespace app::lookup
