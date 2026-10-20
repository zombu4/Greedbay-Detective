#include "lookup/Identity.h"

#include "lookup/Text.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace app::lookup {

namespace {

const QString kNumber = QStringLiteral("\\d+(?:[.,]\\d+)?");

// One alternative per unit, longest first, each escaped.
QString unitAlternatives(const QStringList &units)
{
    QStringList sorted = units;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    QStringList escaped;
    for (const QString &unit : std::as_const(sorted))
        escaped.append(QRegularExpression::escape(unit));
    return escaped.join(QLatin1Char('|'));
}

// Compiled once per pattern and thread (case-insensitive): the matcher runs
// these for every word of every result row. Returned by value: copies share
// the compiled pattern.
QRegularExpression cachedRegex(const QString &pattern)
{
    thread_local QHash<QString, QRegularExpression> cache;
    auto it = cache.constFind(pattern);
    if (it == cache.constEnd())
        it = cache.insert(pattern, QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
    return *it;
}

bool pureDigits(const QString &word)
{
    return !word.isEmpty() && std::all_of(word.cbegin(), word.cend(), [](QChar c) { return c.isDigit(); });
}

bool pureLetters(const QString &word)
{
    return !word.isEmpty() && std::all_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetter(); });
}

// A piece of a range or a slash pair: an optional sign ("+-", "±", "+"),
// then a number with an optional unit, or two such numbers joined by a dash.
// *sides: how many numbers; *unit: a unit is named. False when it is not
// such a piece.
bool isSpecPiece(QString piece, const QRegularExpression &number, const QRegularExpression &numberWithUnit, int *sides,
                 bool *unit)
{
    for (const QString &sign : {QStringLiteral("+-"), QStringLiteral("\u00B1"), QStringLiteral("+")}) {
        if (piece.startsWith(sign)) {
            piece.remove(0, sign.size());
            break;
        }
    }
    static const QRegularExpression dash(QStringLiteral("[-\\x{2013}]"));
    const QStringList parts = piece.split(dash);
    if (parts.size() > 2)
        return false;
    for (const QString &part : parts) {
        if (!number.match(part).hasMatch())
            return false;
        *unit = *unit || numberWithUnit.match(part).hasMatch();
    }
    *sides = static_cast<int>(parts.size());
    return true;
}

bool isUnitWord(const QString &word, const QStringList &units)
{
    return std::any_of(units.cbegin(), units.cend(),
                       [&word](const QString &unit) { return word.compare(unit, Qt::CaseInsensitive) == 0; });
}

// The names that apply to the identity's model: unscoped names always,
// scoped ones ("onlyFor") only for a known model of that category.
void applyNames(Identity *identity)
{
    identity->matchNames.clear();
    identity->searchNames.clear();
    for (const BrandName &name : std::as_const(identity->allNames)) {
        if (!name.onlyFor.isEmpty() && name.onlyFor.compare(identity->category, Qt::CaseInsensitive) != 0)
            continue;
        identity->matchNames.append(name.name);
        if (name.search)
            identity->searchNames.append(name.name);
    }
}

void setBrand(Identity *identity, const Brand &brand, const QString &shownName)
{
    identity->brandId = brand.id;
    identity->brandName = shownName.isEmpty() ? brand.names.first().name : shownName;
    identity->allNames = brand.names;
    applyNames(identity);
}

void setNewBrand(Identity *identity, const QString &name)
{
    identity->brandId = QStringLiteral("user:") + name.simplified().toLower();
    identity->brandName = name.simplified();
    identity->allNames = {BrandName{identity->brandName, true, QString()}};
    applyNames(identity);
}

// The normalized codes (models and family members) of the brand's other
// known models that contain `model`'s code: "Game Boy Color" for "Game Boy",
// "Super NES" for "NES", "Macintosh Classic II" for "Macintosh".
QStringList longerModelsOf(const KnownModels &known, const KnownModel &model)
{
    const QString code = normalizeCode(model.model);
    QStringList own{code};
    for (const QString &member : model.family)
        own.append(normalizeCode(member));
    QStringList longer;
    for (const KnownModel *other : known.modelsOf(model.brandId)) {
        if (other == &model)
            continue;
        QStringList codes{other->model};
        codes.append(other->family);
        for (const QString &otherCode : std::as_const(codes)) {
            const QString normalized = normalizeCode(otherCode);
            if (normalized.size() > code.size() && normalized.contains(code) && !own.contains(normalized)
                && !longer.contains(normalized))
                longer.append(normalized);
        }
    }
    return longer;
}

// Every other known model with a category, as phrases of normalized words:
// the code alone when it has a letter and a digit and at least 3
// characters (C64, A500, TI-99/4A), else "<brand name> <code>" for each
// name of its brand ("Sega CD", "Atari 7800"), so plain words and numbers
// count only with their maker (R6, R7).
void setOtherModels(Identity *identity, const KnownModel &own, const KnownModels &known)
{
    identity->otherModels.clear();
    identity->otherModelCategories.clear();
    QStringList ownCodes{normalizeCode(own.model)};
    for (const QString &member : own.family)
        ownCodes.append(normalizeCode(member));
    for (const Brand &brand : known.brands()) {
        for (const KnownModel *other : known.modelsOf(brand.id)) {
            if (other == &own || other->category.isEmpty())
                continue;
            QStringList codes{other->model};
            codes.append(other->family);
            for (const QString &code : std::as_const(codes)) {
                if (ownCodes.contains(normalizeCode(code)))
                    continue;
                const QStringList codeWords = titleWords(code);
                QList<QStringList> phrases;
                if (hasLetterAndDigit(code) && normalizeCode(code).size() >= 3) {
                    phrases.append(codeWords);
                } else {
                    for (const BrandName &name : brand.names)
                        phrases.append(titleWords(name.name) + codeWords);
                }
                for (QStringList phrase : std::as_const(phrases)) {
                    for (QString &word : phrase)
                        word = normalizeCode(word);
                    identity->otherModels.append(phrase);
                    identity->otherModelCategories.append(other->category);
                }
            }
        }
    }
}

void setKnownModel(Identity *identity, const KnownModel &model, const KnownModels &known)
{
    identity->model = model.model;
    identity->longerModels = longerModelsOf(known, model);
    setOtherModels(identity, model, known);
    identity->family = model.family;
    identity->searchFamily = model.searchFamily;
    identity->productName = model.productName;
    identity->category = model.category;
    identity->notes = model.notes;
    applyNames(identity);
}

// The known brand whose name appears first in `text` (as whole words; at
// the same place the longer name wins); *found is the name as it stands
// there.
const Brand *firstBrandIn(const QString &text, const KnownModels &known, QString *found)
{
    const QStringList words = titleWords(text);
    const Brand *best = nullptr;
    qsizetype bestAt = -1;
    qsizetype bestLength = 0;
    for (const Brand &brand : known.brands()) {
        for (const BrandName &name : brand.names) {
            const QStringList parts = titleWords(name.name);
            const QList<qsizetype> at = phrasePositions(words, parts.join(QLatin1Char(' ')));
            if (at.isEmpty())
                continue;
            if (!best || at.first() < bestAt || (at.first() == bestAt && parts.size() > bestLength)) {
                best = &brand;
                bestAt = at.first();
                bestLength = parts.size();
                *found = words.mid(at.first(), parts.size()).join(QLatin1Char(' '));
            }
        }
    }
    return best;
}

// Every known brand named in `text`, in the order they stand (at the same
// place the longer name first), with the name as it stands there.
QList<QPair<const Brand *, QString>> brandsIn(const QString &text, const KnownModels &known)
{
    const QStringList words = titleWords(text);
    struct Hit {
        const Brand *brand;
        qsizetype at;
        qsizetype length;
        QString name;
    };
    QList<Hit> hits;
    for (const Brand &brand : known.brands()) {
        for (const BrandName &name : brand.names) {
            const QStringList parts = titleWords(name.name);
            const QList<qsizetype> at = phrasePositions(words, parts.join(QLatin1Char(' ')));
            if (!at.isEmpty())
                hits.append({&brand, at.first(), parts.size(), words.mid(at.first(), parts.size()).join(QLatin1Char(' '))});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return a.at != b.at ? a.at < b.at : a.length > b.length;
    });
    QList<QPair<const Brand *, QString>> result;
    for (const Hit &hit : std::as_const(hits)) {
        if (std::none_of(result.cbegin(), result.cend(), [&hit](const auto &entry) { return entry.first == hit.brand; }))
            result.append({hit.brand, hit.name});
    }
    return result;
}

// A known model of `brand` in `list`: one word or 2-4 neighbouring words
// written together ("IM 13"), the longest first; in a title, a digits-only
// model only right after a brand name or its product name.
const KnownModel *knownModelIn(const QStringList &list, const Brand &brand, const KnownModels &known, bool inTitle,
                               const MatchWords &words)
{
    QStringList brandNames;
    for (const BrandName &name : brand.names)
        brandNames.append(name.name);
    for (qsizetype i = 0; i < list.size(); ++i) {
        for (qsizetype n = std::min<qsizetype>(4, list.size() - i); n >= 1; --n) {
            const QString joined = list.mid(i, n).join(QString());
            const KnownModel *model = known.findModel(brand.id, joined);
            if (!model)
                continue;
            if (inTitle && isDigitsOnly(joined)
                && !positionsAfterNames(list, namesBeforeModel(brandNames + QStringList{model->productName}, words)).contains(i))
                continue;
            return model;
        }
    }
    return nullptr;
}

// A number that is a known model of the brand without its letters ("53"
// for LC53, owner case 2) is no candidate: the app asks and suggests that
// model.
bool shortOfKnownModel(const QString &candidate, const QString &brandId, const KnownModels &known)
{
    if (!pureDigits(candidate) || brandId.isEmpty())
        return false;
    for (const KnownModel *model : known.modelsOf(brandId)) {
        QStringList codes{model->model};
        codes.append(model->family);
        for (const QString &code : std::as_const(codes)) {
            const QString normalized = normalizeCode(code);
            if (normalized.size() > candidate.size() && normalized.endsWith(candidate) && !pureDigits(normalized))
                return true;
        }
    }
    return false;
}

// Several candidates of one text: tube types are dropped when anything else
// is left (S3); then, when the only other candidate is the text's last word
// (a seller's stock code, S4) and the one left stands right after the
// brand, that one.
QStringList narrowed(QStringList candidates, const QStringList &afterBrand, const QStringList &textWords,
                     const MatchWords &words)
{
    if (candidates.size() > 1) {
        QStringList noTubes;
        for (const QString &candidate : std::as_const(candidates)) {
            if (!words.tubeCodes.contains(normalizeCode(candidate)))
                noTubes.append(candidate);
        }
        if (!noTubes.isEmpty())
            candidates = noTubes;
    }
    if (candidates.size() == 2 && !textWords.isEmpty()) {
        const QString last = normalizeCode(textWords.last());
        for (qsizetype k = 0; k < 2; ++k) {
            const QString &kept = candidates.at(k);
            const QString &other = candidates.at(1 - k);
            if (afterBrand.contains(kept) && !afterBrand.contains(other) && normalizeCode(other) == last)
                return {kept};
        }
    }
    return candidates;
}

} // namespace

QStringList namesBeforeModel(const QStringList &names, const MatchWords &matchWords)
{
    QStringList result;
    for (const QString &name : names) {
        if (name.simplified().isEmpty())
            continue;
        result.append(name);
        for (const QString &word : matchWords.modelWords)
            result.append(name + QLatin1Char(' ') + word);
    }
    return result;
}

bool matchesAnyPattern(const QString &word, const QStringList &patterns)
{
    return std::any_of(patterns.cbegin(), patterns.cend(),
                       [&word](const QString &pattern) { return cachedRegex(pattern).match(word).hasMatch(); });
}

bool isSpecification(const QStringList &words, qsizetype index, const MatchWords &matchWords)
{
    const QString word = words.at(index);
    const QString units = unitAlternatives(matchWords.specUnits + matchWords.specUnitsGlued);
    const QString pairUnits = unitAlternatives(matchWords.specUnits + matchWords.specUnitsGlued + matchWords.specUnitsInPairs);
    // "1500V", "16GB", "2-CHANNEL".
    if (cachedRegex(QStringLiteral("^%1-?(?:%2)$").arg(kNumber, units)).match(word).hasMatch())
        return true;
    // A range ("0-1500V", "0-15A") or a slash pair ("2M/6M", "0-8V/3A").
    const QRegularExpression number = cachedRegex(QStringLiteral("^%1(?:%2)?$").arg(kNumber, pairUnits));
    const QRegularExpression numberWithUnit = cachedRegex(QStringLiteral("^%1(?:%2)$").arg(kNumber, pairUnits));
    const QStringList pieces = word.split(QLatin1Char('/'));
    bool unit = false;
    bool allPieces = true;
    int sides = 0;
    for (const QString &piece : pieces)
        allPieces = allPieces && isSpecPiece(piece, number, numberWithUnit, &sides, &unit);
    if (allPieces && unit && (pieces.size() > 1 || sides == 2))
        return true;
    for (const QString &prefix : matchWords.specPrefixes) {
        if (cachedRegex(QStringLiteral("^%1-%2(?:%3)?$").arg(QRegularExpression::escape(prefix), kNumber, units)).match(word).hasMatch())
            return true;
    }
    return !word.isEmpty() && word.back().isDigit() && index + 1 < words.size()
           && isUnitWord(words.at(index + 1), matchWords.specUnits);
}

bool isModelLike(const QStringList &words, qsizetype index, const MatchWords &matchWords)
{
    const QString &word = words.at(index);
    if (!hasLetterAndDigit(word) || isSpecification(words, index, matchWords) || matchesAnyPattern(word, matchWords.notModels))
        return false;
    // An option code: right after an option word, or after other codes that
    // follow one ("Opt 1E5 1EA").
    for (qsizetype k = index - 1; k >= 0 && k >= index - 4; --k) {
        if (matchWords.optionWords.contains(words.at(k), Qt::CaseInsensitive))
            return false;
        if (!hasLetterAndDigit(words.at(k)) && !pureDigits(words.at(k)))
            break;
    }
    return true;
}

QStringList modelCandidatesAfterBrand(const QString &text, const QStringList &brandNames, const MatchWords &matchWords,
                                      QStringList *afterBrand)
{
    const QStringList words = titleWords(text);
    const QList<qsizetype> after = positionsAfterNames(words, namesBeforeModel(brandNames, matchWords));
    QStringList result;
    QStringList seen;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString &word = words.at(i);
        QString candidate;
        if (isModelLike(words, i, matchWords)) {
            if (std::any_of(brandNames.cbegin(), brandNames.cend(),
                            [&word](const QString &name) { return word.compare(name, Qt::CaseInsensitive) == 0; }))
                continue;
            candidate = word.toUpper();
        } else if (after.contains(i) && pureDigits(word) && !isSpecification(words, i, matchWords)) {
            // "Tektronix 2445", "Racal-Dana 1992" (S17).
            candidate = word;
        } else if (after.contains(i) && i + 1 < words.size() && word.size() <= 3 && pureLetters(word)
                   && !matchWords.notCodePrefixes.contains(word, Qt::CaseInsensitive) && pureDigits(words.at(i + 1))
                   && !isSpecification(words, i + 1, matchWords)) {
            // "Sencore TC 154" -> TC154 (S18).
            candidate = (word + words.at(i + 1)).toUpper();
        }
        if (candidate.isEmpty())
            continue;
        const QString code = normalizeCode(candidate);
        if (seen.contains(code))
            continue;
        seen.append(code);
        result.append(candidate);
        if (after.contains(i))
            afterBrand->append(candidate);
    }
    return result;
}

Identity identityFromUser(const ConfirmedIdentity &entered, const KnownModels &known, const QString &source)
{
    Identity identity;
    identity.source = source;
    const QString brandText = entered.brand.simplified();
    if (const Brand *brand = known.brandByName(brandText))
        setBrand(&identity, *brand, brandText);
    else if (!brandText.isEmpty())
        setNewBrand(&identity, brandText);
    const QString modelText = entered.model.simplified();
    if (const KnownModel *model = identity.brandId.isEmpty() ? nullptr : known.findModel(identity.brandId, modelText))
        setKnownModel(&identity, *model, known);
    else
        identity.model = modelText;
    if (!entered.productName.simplified().isEmpty())
        identity.productName = entered.productName.simplified();
    identity.confident = !identity.brandId.isEmpty() && !identity.model.isEmpty();
    if (!identity.confident)
        identity.reason = QStringLiteral("a brand and a model are needed");
    return identity;
}

Identity identify(const ItemRecord &item, const KnownModels &known, const MatchWords &words)
{
    if (const std::optional<ConfirmedIdentity> saved = known.confirmedFor(item.itemId))
        return identityFromUser(*saved, known, QStringLiteral("saved"));

    Identity identity;
    QString brandSpecific = item.specific(QStringLiteral("Brand")).simplified();
    // "Unbranded", "Does not apply", ...: no brand is named there.
    if (words.notBrands.contains(brandSpecific, Qt::CaseInsensitive))
        brandSpecific.clear();
    QString found;
    const Brand *brand = nullptr;
    if (!brandSpecific.isEmpty()) {
        brand = known.brandByName(brandSpecific);
        if (brand)
            found = brandSpecific;
        else
            brand = firstBrandIn(brandSpecific, known, &found);
    } else {
        // Several brands named in the title ("Analogue Heathkit IM-13"): the
        // first one with a known model there, else the first one.
        const QList<QPair<const Brand *, QString>> named = brandsIn(item.title, known);
        const QStringList titleList = titleWords(item.title);
        for (const auto &entry : named) {
            if (knownModelIn(titleList, *entry.first, known, true, words)) {
                brand = entry.first;
                found = entry.second;
                break;
            }
        }
        if (!brand && !named.isEmpty()) {
            brand = named.first().first;
            found = named.first().second;
        }
    }
    if (brand)
        setBrand(&identity, *brand, found);
    else if (!brandSpecific.isEmpty())
        setNewBrand(&identity, brandSpecific);

    const struct {
        QString text;
        const char *source;
        const char *field;
    } sources[] = {
        {item.specific(QStringLiteral("Model")), "item specifics", "Model specific"},
        {item.specific(QStringLiteral("MPN")), "item specifics", "MPN specific"},
        {item.title, "title", "title"},
    };

    // A known model of the brand, anywhere: one word or 2-4 neighbouring
    // words written together ("IM 13"), the longest first.
    if (brand) {
        for (const auto &source : sources) {
            const bool inTitle = &source == &sources[std::size(sources) - 1];
            if (const KnownModel *model = knownModelIn(titleWords(source.text), *brand, known, inTitle, words)) {
                setKnownModel(&identity, *model, known);
                identity.source = QStringLiteral("known model");
            }
            if (!identity.model.isEmpty())
                break;
        }
    }

    QStringList allCandidates;
    if (identity.model.isEmpty()) {
        for (const auto &source : sources) {
            QStringList afterBrand;
            QStringList found = modelCandidatesAfterBrand(source.text, identity.matchNames, words, &afterBrand);
            found.removeIf([&](const QString &candidate) { return shortOfKnownModel(candidate, identity.brandId, known); });
            for (const QString &candidate : found) {
                if (!allCandidates.contains(candidate))
                    allCandidates.append(candidate);
            }
            const QStringList candidates = narrowed(found, afterBrand, titleWords(source.text), words);
            if (candidates.size() == 1 && identity.reason.isEmpty()) {
                identity.model = candidates.first();
                identity.source = QString::fromLatin1(source.source);
                break;
            }
            if (candidates.size() > 1 && identity.reason.isEmpty())
                identity.reason = QStringLiteral("several model-like words in the %1: %2")
                                      .arg(QString::fromLatin1(source.field), candidates.join(QStringLiteral(", ")));
        }
        if (identity.model.isEmpty() && identity.reason.isEmpty())
            identity.reason = QStringLiteral("no model number in the title or the item specifics");
    }
    if (identity.brandId.isEmpty())
        identity.reason = QStringLiteral("no brand found in the item specifics or the title");

    identity.confident = !identity.brandId.isEmpty() && !identity.model.isEmpty() && identity.reason.isEmpty();
    if (!identity.confident) {
        identity.candidates = allCandidates;
        if (!identity.brandId.isEmpty()) {
            for (const KnownModel *model : known.modelsOf(identity.brandId)) {
                if (!identity.candidates.contains(model->model))
                    identity.candidates.append(model->model);
            }
        }
    }
    return identity;
}

QStringList buildQueries(const Identity &identity)
{
    QStringList queries;
    if (!identity.canSearch())
        return queries;
    // The model, then (when the data says so) each family member: eBay's
    // website help documents "(a OR b)" but not the "(a,b)" form for the
    // search box (it is documented only for eBay's developer API, KB 2190),
    // so every member is its own query, each a page load of its own.
    QStringList codes{identity.model};
    if (identity.searchFamily)
        codes.append(identity.family);
    for (const QString &name : identity.searchNames) {
        for (const QString &code : std::as_const(codes)) {
            const QString query = name + QLatin1Char(' ') + code;
            if (!queries.contains(query, Qt::CaseInsensitive))
                queries.append(query);
        }
    }
    return queries;
}

} // namespace app::lookup
