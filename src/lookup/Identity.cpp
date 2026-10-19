#include "lookup/Identity.h"

#include "lookup/Text.h"

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

bool matchesAny(const QString &word, const QStringList &patterns)
{
    return std::any_of(patterns.cbegin(), patterns.cend(), [&word](const QString &pattern) {
        return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(word).hasMatch();
    });
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

void setKnownModel(Identity *identity, const KnownModel &model)
{
    identity->model = model.model;
    identity->family = model.family;
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

} // namespace

bool isSpecification(const QStringList &words, qsizetype index, const MatchWords &matchWords)
{
    const QString word = words.at(index);
    const QString units = unitAlternatives(matchWords.specUnits);
    const QRegularExpression withUnit(QStringLiteral("^%1(?:%2)$").arg(kNumber, units), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression part(QStringLiteral("^%1(?:%2)?$").arg(kNumber, units), QRegularExpression::CaseInsensitiveOption);
    if (withUnit.match(word).hasMatch())
        return true;
    const QStringList sides = word.split(QRegularExpression(QStringLiteral("[-\\x{2013}]")));
    if (sides.size() == 2 && part.match(sides.at(0)).hasMatch() && part.match(sides.at(1)).hasMatch()
        && (withUnit.match(sides.at(0)).hasMatch() || withUnit.match(sides.at(1)).hasMatch()))
        return true;
    for (const QString &prefix : matchWords.specPrefixes) {
        const QRegularExpression prefixed(QStringLiteral("^%1-%2(?:%3)?$").arg(QRegularExpression::escape(prefix), kNumber, units),
                                          QRegularExpression::CaseInsensitiveOption);
        if (prefixed.match(word).hasMatch())
            return true;
    }
    return !word.isEmpty() && word.back().isDigit() && index + 1 < words.size()
           && isUnitWord(words.at(index + 1), matchWords.specUnits);
}

bool isModelLike(const QStringList &words, qsizetype index, const MatchWords &matchWords)
{
    const QString &word = words.at(index);
    return hasLetterAndDigit(word) && !isSpecification(words, index, matchWords) && !matchesAny(word, matchWords.notModels);
}

QStringList modelCandidates(const QString &text, const QStringList &brandNames, const MatchWords &matchWords)
{
    const QStringList words = titleWords(text);
    QStringList result;
    QStringList seen;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString &word = words.at(i);
        if (!isModelLike(words, i, matchWords))
            continue;
        if (std::any_of(brandNames.cbegin(), brandNames.cend(),
                        [&word](const QString &name) { return word.compare(name, Qt::CaseInsensitive) == 0; }))
            continue;
        const QString code = normalizeCode(word);
        if (seen.contains(code))
            continue;
        seen.append(code);
        result.append(word.toUpper());
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
        setKnownModel(&identity, *model);
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
        brand = firstBrandIn(item.title, known, &found);
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

    // A known model of the brand, anywhere: one word or 2-3 neighbouring
    // words written together ("IM 13").
    if (brand) {
        for (const auto &source : sources) {
            const QStringList list = titleWords(source.text);
            for (qsizetype i = 0; i < list.size() && identity.model.isEmpty(); ++i) {
                QString joined;
                for (qsizetype n = 0; n < 3 && i + n < list.size(); ++n) {
                    joined += list.at(i + n);
                    if (const KnownModel *model = known.findModel(brand->id, joined)) {
                        setKnownModel(&identity, *model);
                        identity.source = QStringLiteral("known model");
                        break;
                    }
                }
            }
            if (!identity.model.isEmpty())
                break;
        }
    }

    QStringList allCandidates;
    if (identity.model.isEmpty()) {
        for (const auto &source : sources) {
            const QStringList candidates = modelCandidates(source.text, identity.matchNames, words);
            for (const QString &candidate : candidates) {
                if (!allCandidates.contains(candidate))
                    allCandidates.append(candidate);
            }
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
    for (const QString &name : identity.searchNames) {
        const QString query = name + QLatin1Char(' ') + identity.model;
        if (!queries.contains(query, Qt::CaseInsensitive))
            queries.append(query);
    }
    return queries;
}

} // namespace app::lookup
