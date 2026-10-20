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
// one of the notBrands values ("Unbranded", also "Unbranded/Generic": each
// "/" part one). Without a usable "Brand" specific, the first known brand
// named in the title that has a known model there, else the known brand
// whose name appears first in the title, else (owner case 12) the title's
// first word after any titleOpeners, when it is letters only and the next
// word is model-like ("FNIRSI 1014D ..." -> FNIRSI). Values of the
// productCodeLabels specifics (ASIN, UPC, ...) are never models.
//
// Model (owner case 11: the listing's own title first, then the "Model"
// specific, then the "MPN" specific):
//   1 the title: a known model (or family member) of that brand as a whole
//     word or 2-4 neighbouring words written together (the longest first;
//     a known model of digits only only right after a brand name or its
//     product name - owner decision 2026-10-19: "1992" is a model, a year
//     elsewhere); else the candidate right after a brand name ("<brand>" or
//     "<brand> model"); else the title's only candidate (after narrowed():
//     tube types out, a seller's code at the end out). Its source is "item
//     specifics" when a specific names the same code, else "title".
//   2 else the "Model", then the "MPN" specific, but only a code the title
//     holds too: a specific whose code is not in the title is ignored (an
//     MPN is often a seller's own SKU: "TXD-A219" for a Hantek DSO2C10).
// Candidates (modelCandidatesAfterBrand): the model-like words
// (isModelLike: a letter and a digit, numbers-only never, no specification,
// not a count, version or option code) that are no brand name, in upper
// case with its hyphens kept as written (AA-100, IM-13, LC53, 8904A); and,
// right after a brand name, a number of digits only ("Tektronix 2445",
// brand-scan S17) or a short letter word and a number written apart, joined
// ("Sencore TC 154" -> TC154, S18); a number that is a known model of the
// brand without its letters ("Sencore 53" for LC53, owner case 2) is none.
// Elsewhere words are never joined ("53 Z Meter" never gives 53Z). Anything
// else - no brand, no model, several title candidates and none right after
// the brand, or only a specific the title does not hold - is not confident:
// the app asks the user (rule 26) and suggests the candidates and the known
// models of the brand.
struct Identity {
    QString brandId;           // empty when no brand is known
    QString brandName;         // shown: the name found, or the first name
    QStringList matchNames;    // every name of the brand that applies to the model
    QStringList searchNames;   // the names used in queries
    QList<BrandName> allNames; // every name of the brand, with its scope
    QString model;             // manufacturer's spelling; empty when unknown
    QStringList family;        // codes that count as the same item
    // Normalized codes of the brand's other known models (and their family
    // members) that contain this model's code ("Game Boy" -> GAMEBOYCOLOR,
    // "NES" -> SUPERNES, PR57 -> PR570): never this item.
    QStringList longerModels;
    bool searchFamily = false; // the family members are searched too
    QString productName;
    QString category;          // of a known model; empty otherwise
    QStringList notes;         // known-issue notes of the model
    // Other known models with a category, normalized word by word, each as
    // its code alone (a code with a letter and a digit, at least 3 long:
    // C64, A500) or as "<brand name> <code>" ("Sega CD", "Atari 7800"); with
    // their categories. For a system (MatchWords::systemCategories) a title
    // naming one of the other systems is a multi-system accessory (R6).
    QList<QStringList> otherModels;
    QStringList otherModelCategories;
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

// One query per search name of the brand: "<name> <model>" (rules 21, 23),
// and, for a known model whose data sets searchFamily, also "<name>
// <member>" for every family member (owner case 10). No model-only queries
// and no spelling variants. Empty when the identity cannot be searched.
QStringList buildQueries(const Identity &identity);

// True when words[index] is a specification, not a model (rule 24,
// brand-scan S1, retro-scan R5 and R9): a number with a unit glued on
// ("1500V", "50kv", "600kHz", "45%", "16GB", "250MSa/s"; units from
// specUnits and specUnitsGlued) or joined by a hyphen ("2-CHANNEL",
// "14-PIN"); a range of numbers with a unit ("10kHz-1000MHz", "0-1500V",
// "0-15A"; the pair units A and M count here); a slash pair of such pieces,
// optionally signed ("2M/6M", "0-8V/3A", "+-25V/1A"); a current of 1-3
// digits ("30A", currentUnits) when another word of the title states a voltage
// (voltageUnits: "25V", "0-25V/7A"); a spec prefix with a
// number and an optional unit ("DC-600", "DC-600kHz"); or a word ending in a
// digit that is followed by a specUnits word ("600 KHZ", "15 MEG",
// "4 channels").
bool isSpecification(const QStringList &words, qsizetype index, const MatchWords &matchWords);

// True when words[index] can be a model code: a letter and a digit, no
// specification, none of the notModels patterns (1960s, 3rd, 4pcs, Mk2,
// 1080P, DB9, RS232, V1.15, REV.6A), and no option code (it follows an
// optionWords word, possibly after other codes: "Opt 1E5 1EA", S2).
bool isModelLike(const QStringList &words, qsizetype index, const MatchWords &matchWords);

// The model-like words of `text` (see above) that are no brand name, in
// upper case, plus, right after one of `brandNames` (or "<name>
// <modelWords word>"), a digits-only number that is no specification or a
// short letter word (1-3 letters, not a notCodePrefixes word) and a
// digits-only number written apart, joined in upper case. In the order they
// stand, without repeats by normalized code. *afterBrand gets the
// candidates that stand right after a brand name.
QStringList modelCandidatesAfterBrand(const QString &text, const QStringList &brandNames, const MatchWords &matchWords,
                                      QStringList *afterBrand);

// `names` and, for each, "<name> <word>" for every modelWords word: the
// names right after which a model may stand ("Racal-Dana model 1992").
QStringList namesBeforeModel(const QStringList &names, const MatchWords &matchWords);

// True when `word` matches one of the regular expressions `patterns`,
// without regard to case.
bool matchesAnyPattern(const QString &word, const QStringList &patterns);

} // namespace app::lookup
