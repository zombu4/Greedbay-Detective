#include "model/Guards.h"

#include "lookup/Text.h"

#include <QRegularExpression>

namespace app::model {

namespace {

// G5: words the model offered as brands in the 2026-10-20 runs, or that are
// no maker by their nature (a place, an age, a state, a colour, a material,
// a part shape). The rules' notBrands and titleOpeners are refused too.
const QStringList &junkBrands()
{
    static const QStringList words{
        QStringLiteral("made in usa"), QStringLiteral("made in japan"), QStringLiteral("made in germany"),
        QStringLiteral("made in china"), QStringLiteral("usa"),         QStringLiteral("vintage"),
        QStringLiteral("antique"),     QStringLiteral("retro"),         QStringLiteral("lot"),
        QStringLiteral("bayonet"),     QStringLiteral("axial"),         QStringLiteral("radial"),
        QStringLiteral("nos"),         QStringLiteral("new"),           QStringLiteral("used"),
        QStringLiteral("rare"),        QStringLiteral("unknown"),       QStringLiteral("none"),
        QStringLiteral("n/a"),         QStringLiteral("na"),            QStringLiteral("other"),
        QStringLiteral("black"),       QStringLiteral("white"),         QStringLiteral("silver"),
        QStringLiteral("gray"),        QStringLiteral("grey"),          QStringLiteral("red"),
        QStringLiteral("blue"),        QStringLiteral("green"),         QStringLiteral("brass"),
        QStringLiteral("steel"),       QStringLiteral("copper"),        QStringLiteral("aluminum"),
        QStringLiteral("aluminium"),   QStringLiteral("wood"),          QStringLiteral("plastic"),
        QStringLiteral("glass"),       QStringLiteral("leather"),       QStringLiteral("chrome"),
        QStringLiteral("ceramic"),     QStringLiteral("bakelite"),      QStringLiteral("tested"),
        QStringLiteral("untested"),    QStringLiteral("working"),       QStringLiteral("broken"),
        QStringLiteral("parts"),       QStringLiteral("repair"),
    };
    return words;
}

QString folded(const QString &text) { return text.simplified().toLower(); }

// A brand or word as the title would hold it: its title words, lower case,
// joined by spaces ("Unbranded." -> "unbranded").
QString wordsOf(const QString &text) { return lookup::titleWords(text).join(QLatin1Char(' ')).toLower(); }

bool listHas(const QStringList &list, const QString &value)
{
    const QString wanted = wordsOf(value);
    for (const QString &entry : list) {
        if (wordsOf(entry) == wanted)
            return true;
    }
    return false;
}

// True when `name` stands in `text` as whole words.
bool hasWords(const QString &text, const QString &name)
{
    const QStringList nameWords = lookup::listWords(name);
    if (nameWords.isEmpty())
        return false;
    return !lookup::phrasePositions(lookup::listWords(text), nameWords.join(QLatin1Char(' '))).isEmpty();
}

// The positions in `words` where 1-4 neighbouring words written together
// give `code` (normalized), with the number of words.
QList<QPair<qsizetype, qsizetype>> codePositions(const QStringList &words, const QString &code)
{
    QList<QPair<qsizetype, qsizetype>> found;
    const QString wanted = lookup::normalizeCode(code);
    if (wanted.isEmpty())
        return found;
    for (qsizetype start = 0; start < words.size(); ++start) {
        QString joined;
        for (qsizetype count = 1; count <= 4 && start + count <= words.size(); ++count) {
            joined += lookup::normalizeCode(words.at(start + count - 1));
            if (joined == wanted)
                found.append({start, count});
            if (joined.size() >= wanted.size())
                break;
        }
    }
    return found;
}

// The title's first word that is no title opener ("New", "Vintage").
QString firstMakerWord(const QStringList &titleWords, const lookup::MatchWords &words)
{
    for (const QString &word : titleWords) {
        if (!listHas(words.titleOpeners, word))
            return word;
    }
    return {};
}

Verdict refused(const QString &guard, const QString &why) { return {false, guard, why, {}}; }

} // namespace

Verdict checkIdentity(const IdentityAnswer &answer, const lookup::ItemRecord &item, const lookup::KnownModels &known,
                      const lookup::MatchWords &words)
{
    if (answer.brand.isEmpty() || answer.model.isEmpty())
        return refused(QStringLiteral("G2"), QStringLiteral("no brand or no model given"));

    // G5
    for (const QString &part : answer.brand.split(QLatin1Char('/'))) {
        if (wordsOf(part).isEmpty() || listHas(words.notBrands, part) || listHas(junkBrands(), part)
            || listHas(words.titleOpeners, part))
            return refused(QStringLiteral("G5"), QStringLiteral("\"%1\" names no maker").arg(answer.brand));
    }
    const lookup::Brand *brand = known.brandByName(answer.brand);
    const QStringList titleWords = lookup::titleWords(item.title);
    // The listing's own "Brand" specific, when it names a maker, is the brand
    // (the rules take it so): another brand is refused.
    const QString specificBrand = item.specific(QStringLiteral("Brand"));
    bool specificIsMaker = !wordsOf(specificBrand).isEmpty();
    for (const QString &part : specificBrand.split(QLatin1Char('/')))
        specificIsMaker = specificIsMaker && !listHas(words.notBrands, part);
    const bool brandSpecific = specificIsMaker && wordsOf(specificBrand) == wordsOf(answer.brand);
    if (specificIsMaker && !brandSpecific) {
        const lookup::Brand *specificKnown = known.brandByName(specificBrand);
        if (!brand || !specificKnown || brand->id != specificKnown->id)
            return refused(QStringLiteral("G5"),
                    QStringLiteral("the listing's Brand is \"%1\", not \"%2\"").arg(specificBrand, answer.brand));
    }
    if (!brand && !brandSpecific) {
        const QStringList brandWords = lookup::titleWords(answer.brand);
        const QString first = firstMakerWord(titleWords, words);
        const qsizetype at = titleWords.indexOf(first);
        const bool leading = !brandWords.isEmpty() && at >= 0 && at + brandWords.size() <= titleWords.size()
            && titleWords.mid(at, brandWords.size()).join(QLatin1Char(' ')).toLower() == brandWords.join(QLatin1Char(' ')).toLower();
        if (!leading)
            return refused(QStringLiteral("G5"),
                    QStringLiteral("\"%1\" is no known brand and not the title's first maker word").arg(answer.brand));
    }

    // G2: the brand is quoted, not invented.
    QStringList brandNames{answer.brand};
    if (brand) {
        for (const lookup::BrandName &name : brand->names)
            brandNames.append(name.name);
    }
    QString listingText = item.title;
    for (const lookup::ItemRecord::Specific &specific : item.specifics)
        listingText += QLatin1Char('\n') + specific.value;
    bool brandQuoted = false;
    for (const QString &name : brandNames)
        brandQuoted = brandQuoted || hasWords(listingText, name);
    if (!brandQuoted)
        return refused(QStringLiteral("G2"), QStringLiteral("the brand \"%1\" is not in the listing").arg(answer.brand));

    // G3: product codes never, and the model in the title itself.
    for (const lookup::ItemRecord::Specific &specific : item.specifics) {
        if (listHas(words.productCodeLabels, specific.label)
            && lookup::normalizeCode(specific.value) == lookup::normalizeCode(answer.model))
            return refused(QStringLiteral("G3"), QStringLiteral("\"%1\" is the listing's %2").arg(answer.model, specific.label));
    }
    const lookup::KnownModel *knownModel = brand ? known.findModel(brand->id, answer.model) : nullptr;
    QStringList codes{answer.model};
    if (knownModel) {
        codes.append(knownModel->model);
        codes.append(knownModel->family);
    }
    QList<QPair<qsizetype, qsizetype>> places;
    for (const QString &code : codes)
        places.append(codePositions(titleWords, code));
    if (places.isEmpty())
        return refused(QStringLiteral("G3"), QStringLiteral("the model \"%1\" is not in the listing's title").arg(answer.model));

    // G4 and G6, at the first place it stands.
    const auto [at, count] = places.first();
    for (qsizetype i = at; i < at + count; ++i) {
        if (lookup::isSpecification(titleWords, i, words))
            return refused(QStringLiteral("G4"), QStringLiteral("\"%1\" is a specification, not a model").arg(answer.model));
    }
    if (lookup::isDigitsOnly(answer.model)) {
        // A year (1900-2099) or a count (1-2 digits) is no model unless it is
        // a known one ("Racal-Dana 1992") or the listing's own item specifics
        // name it as the model or MPN ("Tektronix 1910", 2026-10-22); it must
        // still follow the brand (below).
        const QString digits = lookup::normalizeCode(answer.model);
        static const QRegularExpression year(QStringLiteral("^(19|20)\\d\\d$"));
        const bool namedBySpecifics = lookup::normalizeCode(item.specific(QStringLiteral("Model"))) == digits
                                      || lookup::normalizeCode(item.specific(QStringLiteral("MPN"))) == digits;
        if (!knownModel && !namedBySpecifics && (digits.size() <= 2 || year.match(digits).hasMatch()))
            return refused(QStringLiteral("G6"), QStringLiteral("\"%1\" is a year or a count, not a model").arg(answer.model));
        const QList<qsizetype> afterBrand = lookup::positionsAfterNames(titleWords, lookup::namesBeforeModel(brandNames, words));
        if (!afterBrand.contains(at))
            return refused(QStringLiteral("G6"),
                    QStringLiteral("the number \"%1\" does not follow the brand (a year or a count?)").arg(answer.model));
    } else if (!knownModel && !(count == 1 && lookup::isModelLike(titleWords, at, words))) {
        return refused(QStringLiteral("G4"),
                QStringLiteral("\"%1\" is no model code and no known model of the brand").arg(answer.model));
    }
    // The model as the title writes it: a look-alike letter in the answer
    // ("ı" for "I") never reaches the user's fields.
    return {true, {}, {}, titleWords.mid(at, count).join(QLatin1Char(' '))};
}

bool mayAskIdentity(const lookup::Identity &rules)
{
    // Only identities the rules read from the listing (or none at all).
    return rules.source == QLatin1String("title") || rules.source == QLatin1String("item specifics") || rules.source.isEmpty();
}

bool sameIdentity(const IdentityAnswer &answer, const lookup::Identity &rules, const lookup::KnownModels &known)
{
    if (rules.model.isEmpty())
        return false;
    const lookup::Brand *brand = known.brandByName(answer.brand);
    const bool sameBrand = (brand && !rules.brandId.isEmpty() && brand->id == rules.brandId)
        || folded(answer.brand) == folded(rules.brandName);
    if (!sameBrand)
        return false;
    const QString code = lookup::normalizeCode(answer.model);
    if (code == lookup::normalizeCode(rules.model))
        return true;
    for (const QString &member : rules.family) {
        if (code == lookup::normalizeCode(member))
            return true;
    }
    return false;
}

bool mayAskSort(lookup::Bucket rulesBucket)
{
    return rulesBucket == lookup::Bucket::SameItem || rulesBucket == lookup::Bucket::ToCheck;
}

SortEffect sortEffect(lookup::Bucket rulesBucket, bool ownListing, const QString &category, const QString &title,
                      const lookup::Identity &identity, const lookup::MatchWords &words)
{
    if (!mayAskSort(rulesBucket) || category == QLatin1String("unknown") || category.isEmpty())
        return {};
    if (rulesBucket == lookup::Bucket::SameItem) {
        if (category == QLatin1String("same item"))
            return {};
        if (ownListing)
            return {SortEffect::Kind::Refused, QStringLiteral("local model: %1 - not used: this is the listing itself").arg(category),
                    QStringLiteral("G8")};
        return {SortEffect::Kind::ToCheck, QStringLiteral("local model: %1 (the rules: same item) - check it").arg(category), {}};
    }
    // "to check"
    if (category == QLatin1String("same item") && listHas(words.systemCategories, identity.category)) {
        // Words joined by "-" count apart too ("Game-Cartridge").
        QString spaced = title;
        spaced.replace(QLatin1Char('-'), QLatin1Char(' '));
        const QStringList titleList = lookup::listWords(spaced);
        for (const QString &media : words.media) {
            if (!lookup::phrasePositions(titleList, folded(media)).isEmpty())
                return {SortEffect::Kind::Refused,
                        QStringLiteral("local model: same item - not used: the title names %1 for the system").arg(media),
                        QStringLiteral("G9")};
        }
    }
    return {SortEffect::Kind::Suggest, QStringLiteral("local model suggests: %1").arg(category), {}};
}

} // namespace app::model
