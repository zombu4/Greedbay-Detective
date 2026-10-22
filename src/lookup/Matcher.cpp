#include "lookup/Matcher.h"

#include <QRegularExpression>

#include "lookup/Text.h"

#include <algorithm>

namespace app::lookup {

namespace {

constexpr qsizetype kForReach = 3;

// Stands in for a masked title word: no word list holds it.
const QString kMasked = QStringLiteral("\u00B7");

// The first word of `list` found in `words` as a phrase, with its position;
// -1 when none.
qsizetype firstPhrase(const QStringList &words, const QStringList &list, QString *which)
{
    qsizetype best = -1;
    for (const QString &phrase : list) {
        const QList<qsizetype> at = phrasePositions(words, phrase);
        if (!at.isEmpty() && (best < 0 || at.first() < best)) {
            best = at.first();
            *which = phrase;
        }
    }
    return best;
}

// Where the model or a family member stands in the title: one word or 2-4
// neighbouring words written together.
struct Span {
    qsizetype at = 0;
    qsizetype length = 1;
};

QList<Span> modelSpans(const QStringList &words, const Identity &identity)
{
    QStringList codes{normalizeCode(identity.model)};
    for (const QString &member : identity.family)
        codes.append(normalizeCode(member));
    QList<Span> spans;
    for (qsizetype i = 0; i < words.size(); ++i) {
        // A longer known model of the brand covering this word ("Game Boy
        // Color" for "Game Boy", "Super NES" for "NES") is another model.
        bool another = false;
        for (qsizetype from = std::max<qsizetype>(0, i - 3); from <= i && !another; ++from) {
            QString longer;
            for (qsizetype n = 0; n < 5 && from + n < words.size() && !another; ++n) {
                longer += normalizeCode(words.at(from + n));
                another = from + n >= i && identity.longerModels.contains(longer);
            }
        }
        if (another)
            continue;
        QString joined;
        for (qsizetype n = 0; n < 4 && i + n < words.size(); ++n) {
            joined += normalizeCode(words.at(i + n));
            if (codes.contains(joined)) {
                spans.append({i, n + 1});
                break;
            }
        }
    }
    return spans;
}

// The whole code of a span, normalized.
QString codeOf(const QStringList &words, const Span &span)
{
    return normalizeCode(words.mid(span.at, span.length).join(QString()));
}

// Every word position covered by one of the brand's names.
QList<qsizetype> brandPositions(const QStringList &words, const Identity &identity, QString *which)
{
    QList<qsizetype> positions;
    for (const QString &name : identity.matchNames) {
        const QStringList parts = titleWords(name);
        for (const qsizetype at : phrasePositions(words, parts.join(QLatin1Char(' ')))) {
            if (which->isEmpty())
                *which = name;
            for (qsizetype k = 0; k < parts.size(); ++k)
                positions.append(at + k);
        }
    }
    return positions;
}

// Every word position holding a word of the product name ("Z Meter",
// "Passive Probe", "Test Set"): the item's own name is never evidence of
// another thing.
QList<qsizetype> productNamePositions(const QStringList &words, const Identity &identity)
{
    QList<qsizetype> positions;
    const QStringList parts = titleWords(identity.productName);
    for (qsizetype i = 0; i < words.size(); ++i) {
        if (parts.contains(words.at(i), Qt::CaseInsensitive))
            positions.append(i);
    }
    return positions;
}

// `words` with the positions in `covered` replaced by kMasked.
QStringList masked(QStringList words, const QList<qsizetype> &covered)
{
    for (const qsizetype at : covered)
        words[at] = kMasked;
    return words;
}

// The title's words for the word lists, lower case and split at "/", with
// the brand names, the model and the product name's words masked: the lists
// never fire inside them ("Heath Kit" holds no kit, "Game Boy" no game, the
// product name "Test Set" no set; S8, S9, S13, R13).
QStringList listWithoutIdentity(const QStringList &codeList, const QList<qsizetype> &brands, const QList<Span> &models,
                                const Identity &identity)
{
    QList<qsizetype> covered = brands;
    for (const Span &span : models) {
        for (qsizetype k = 0; k < span.length; ++k)
            covered.append(span.at + k);
    }
    covered.append(productNamePositions(codeList, identity));
    return listWords(masked(codeList, covered).join(QLatin1Char(' ')));
}

// Model codes in the title other than the item's own (rule 14). Tube types
// are no other model (S3), nor is the title's last word when the model
// stands before it: sellers end titles with their own stock codes (S4).
// `modelAt`: where the model stands, -1 when not looked at.
QStringList otherModelCodes(const QStringList &words, const Identity &identity, const MatchWords &matchWords,
                            qsizetype modelAt)
{
    QStringList own{normalizeCode(identity.model)};
    for (const QString &member : identity.family)
        own.append(normalizeCode(member));
    // Codes after an extras word come with the item ("7603 mainframe with
    // 7A26 and 7B53A plug-ins").
    qsizetype extrasFrom = words.size();
    for (qsizetype i = 0; i < words.size() && extrasFrom == words.size(); ++i) {
        if (matchWords.withExtras.contains(words.at(i), Qt::CaseInsensitive))
            extrasFrom = i;
    }
    QStringList others;
    for (qsizetype i = 0; i < extrasFrom; ++i) {
        if (!isModelLike(words, i, matchWords))
            continue;
        const QString code = normalizeCode(words.at(i));
        if (own.contains(code) || others.contains(code) || matchWords.tubeCodes.contains(code))
            continue;
        if (i == words.size() - 1 && modelAt >= 0 && modelAt < i)
            continue;
        others.append(code);
    }
    return others;
}

// Another model code of the title (one of `others`) with a bundle join word
// ("and", "&") between it and the model: two items sold together ("<other>
// power analyzer and <model> stereo analyzer"). The code as written, or empty.
QString joinedOtherModel(const QStringList &codeList, const QList<Span> &models, const QStringList &others,
                         const MatchWords &matchWords)
{
    for (qsizetype i = 0; i < codeList.size(); ++i) {
        if (!others.contains(normalizeCode(codeList.at(i))))
            continue;
        for (const Span &span : models) {
            const qsizetype from = std::min(i, span.at);
            const qsizetype to = std::max(i, span.at + span.length - 1);
            for (qsizetype k = from + 1; k < to; ++k) {
                if (matchWords.bundleJoins.contains(codeList.at(k), Qt::CaseInsensitive))
                    return codeList.at(i);
            }
        }
    }
    return {};
}

// True when the phrase starting at `at` (of `length` words) lies inside one
// of the notAccessory phrases ("original case").
bool insideNotAccessory(const QStringList &list, qsizetype at, const MatchWords &matchWords)
{
    for (const QString &phrase : matchWords.notAccessory) {
        const qsizetype length = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        for (const qsizetype start : phrasePositions(list, phrase)) {
            if (start <= at && at < start + length)
                return true;
        }
    }
    return false;
}

// The first word of `phrases` at a position not preceded, within three
// words, by a with-item word ("with manual", "w/ Leads") and not inside a
// notAccessory phrase; -1 when none.
qsizetype firstNotWithItem(const QStringList &words, const QStringList &phrases, const MatchWords &matchWords, QString *which)
{
    for (const QString &phrase : phrases) {
        for (const qsizetype at : phrasePositions(words, phrase)) {
            bool withItem = false;
            for (qsizetype k = 0; k < at && !withItem; ++k)
                withItem = matchWords.withItem.contains(words.at(k), Qt::CaseInsensitive);
            if (!withItem && !insideNotAccessory(words, at, matchWords)) {
                *which = phrase;
                return at;
            }
        }
    }
    return -1;
}

// The phrases of `phrases` that stand anywhere after a with-item word ("with
// leads", "w/ 10 adapters, manual"): they come with the item. For the reason
// text.
QStringList withItemPhrases(const QStringList &words, const QStringList &phrases, const MatchWords &matchWords)
{
    QStringList found;
    for (const QString &phrase : phrases) {
        for (const qsizetype at : phrasePositions(words, phrase)) {
            for (qsizetype k = 0; k < at; ++k) {
                if (matchWords.withItem.contains(words.at(k), Qt::CaseInsensitive)) {
                    found.append(QStringLiteral("\"%1\" after \"%2\"").arg(phrase, words.at(k)));
                    break;
                }
            }
        }
    }
    return found;
}

// True when the identity is a computer, console or handheld (the lists
// that apply only to those).
bool isSystem(const Identity &identity, const MatchWords &matchWords)
{
    return !identity.category.isEmpty() && matchWords.systemCategories.contains(identity.category, Qt::CaseInsensitive);
}

// True when the title names the system itself ("console", "bundle") other
// than right after a companion word ("for console", "for the system"): its
// media and accessories then come with it (R2, R11).
bool itemNamed(const QStringList &list, const MatchWords &matchWords)
{
    for (const QString &noun : matchWords.itemNouns) {
        for (const qsizetype at : phrasePositions(list, noun)) {
            const bool forIt = (at >= 1 && matchWords.companion.contains(list.at(at - 1), Qt::CaseInsensitive))
                               || (at >= 2 && matchWords.companion.contains(list.at(at - 2), Qt::CaseInsensitive));
            if (!forIt)
                return true;
        }
    }
    return false;
}

// A re-issue word right before or after the model ("NES Classic Edition",
// "The C64 Mini", "Flashback 7800"; R4), in the title's words; empty when
// none.
QString reissueNextToModel(const QStringList &words, const QList<Span> &models, const MatchWords &matchWords)
{
    for (const QString &phrase : matchWords.reissue) {
        const qsizetype length = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        for (const qsizetype at : phrasePositions(words, phrase)) {
            if (std::any_of(models.cbegin(), models.cend(),
                            [&](const Span &span) { return at == span.at + span.length || at + length == span.at; }))
                return phrase;
        }
    }
    return {};
}

// Another system named in the title (R6): the first of the identity's
// other models of a system category found in `words` (the title with the
// model masked), as written there; empty when none.
QString otherSystemIn(const QStringList &words, const Identity &identity, const MatchWords &matchWords)
{
    QStringList normalized;
    for (const QString &word : words)
        normalized.append(word == kMasked ? word : normalizeCode(word));
    for (qsizetype p = 0; p < identity.otherModels.size(); ++p) {
        if (!matchWords.systemCategories.contains(identity.otherModelCategories.at(p), Qt::CaseInsensitive))
            continue;
        const QStringList &phrase = identity.otherModels.at(p);
        for (qsizetype i = 0; i + phrase.size() <= normalized.size(); ++i) {
            bool all = !phrase.isEmpty();
            for (qsizetype k = 0; k < phrase.size() && all; ++k)
                all = normalized.at(i + k) == phrase.at(k);
            if (all)
                return words.mid(i, phrase.size()).join(QLatin1Char(' '));
        }
    }
    return {};
}

} // namespace

Match matchRow(const QString &title, const Identity &identity, const MatchWords &words)
{
    Match match;
    QStringList reasons;
    auto done = [&match, &reasons](Bucket bucket) {
        match.bucket = bucket;
        match.reasons = reasons;
        return match;
    };
    if (title.simplified().isEmpty()) {
        reasons.append(QStringLiteral("no title: unknown item"));
        return done(Bucket::UnknownItem);
    }
    const QStringList codeList = titleWords(title);
    const QList<Span> models = identity.model.isEmpty() ? QList<Span>() : modelSpans(codeList, identity);
    QString brandWord;
    const QList<qsizetype> brands = brandPositions(codeList, identity, &brandWord);
    if (models.isEmpty()) {
        reasons.append(QStringLiteral("model %1 not in the title as a whole word").arg(identity.model));
        return done(Bucket::NoMatch);
    }
    // A brand name may also stand inside a slash-joined word ("HP/Agilent").
    if (brands.isEmpty()) {
        const QStringList split = listWords(title);
        for (const QString &name : identity.matchNames) {
            if (!phrasePositions(split, titleWords(name.toLower()).join(QLatin1Char(' '))).isEmpty()) {
                brandWord = name;
                break;
            }
        }
    }
    const QString code = codeOf(codeList, models.first());
    const QString written = codeList.mid(models.first().at, models.first().length).join(QLatin1Char(' '));
    const bool digitsOnly = isDigitsOnly(code);
    const bool lettersOnly = std::none_of(code.cbegin(), code.cend(), [](QChar c) { return c.isDigit(); });
    if (brandWord.isEmpty() && digitsOnly) {
        // Owner decision 2026-10-19: a digits-only model counts only right
        // after a brand name; without the brand it is a year or a number.
        reasons.append(QStringLiteral("%1 without the brand %2: a year or a number, not this item").arg(written, identity.brandName));
        return done(Bucket::NoMatch);
    }
    if (brandWord.isEmpty() && lettersOnly) {
        // A plain-word model needs its maker ("Camputers Lynx" is no Atari
        // Lynx, R7).
        reasons.append(QStringLiteral("%1 without the brand %2: a plain-word model counts only with its maker").arg(written, identity.brandName));
        return done(Bucket::NoMatch);
    }
    if (brandWord.isEmpty()) {
        // The model without the brand: often another product with the same
        // code (cards, car parts), sometimes the item with a short title.
        // Shown for the user to decide, never counted by itself.
        reasons.append(QStringLiteral("model %1 found, but the brand %2 is not in the title: check").arg(written, identity.brandName));
        return done(Bucket::ToCheck);
    }
    // The title's words for the word lists, without the brand, the model and
    // the product name.
    const QStringList list = listWithoutIdentity(codeList, brands, models, identity);
    // A model of digits only ("2022") also stands for years and quantities:
    // it counts only right after a brand name or the product name (also
    // "<name> model"). Elsewhere the row is never counted: documentation
    // stays documentation, anything else is for the user to check.
    QString word;
    QStringList namesBefore = identity.matchNames;
    if (!identity.productName.isEmpty())
        namesBefore.append(identity.productName);
    QList<qsizetype> afterNames = positionsAfterNames(codeList, namesBeforeModel(namesBefore, words));
    // A brand name inside a slash-joined word ("Marconi/IFR 2022").
    for (qsizetype k = 0; k < codeList.size(); ++k) {
        const QStringList parts = listWords(codeList.at(k));
        if (parts.size() > 1 && std::any_of(identity.matchNames.cbegin(), identity.matchNames.cend(), [&parts](const QString &name) {
                return parts.contains(name.toLower());
            }))
            afterNames.append(k + 1);
    }
    // The other words of the brand's own names ("Dynascan" of "B&K Dynascan"):
    // "BK Precision DynaScan 467" has the model right after the brand too.
    QStringList brandNameWords;
    for (const QString &name : identity.matchNames) {
        const QStringList parts = listWords(name);
        for (qsizetype k = 1; k < parts.size(); ++k)
            brandNameWords.append(parts.at(k));
    }
    if (digitsOnly && std::none_of(models.cbegin(), models.cend(), [&](const Span &span) {
            return brands.contains(span.at - 1) || afterNames.contains(span.at)
                   || (span.at >= 2 && brands.contains(span.at - 2)
                       && brandNameWords.contains(codeList.at(span.at - 1), Qt::CaseInsensitive));
        })) {
        if (firstNotWithItem(list, words.documentation, words, &word) >= 0) {
            reasons.append(QStringLiteral("documentation (\"%1\"); %2 does not follow the brand name").arg(word, identity.model));
            return done(Bucket::Documentation);
        }
        reasons.append(QStringLiteral("model %1 is digits only and does not follow the brand name: check").arg(identity.model));
        return done(Bucket::ToCheck);
    }
    reasons.append(QStringLiteral("model %1 and brand %2 found").arg(written, brandWord));

    const bool system = isSystem(identity, words);
    // A key, code or firmware for the item, not the item itself.
    if (firstPhrase(list, words.notTheItem, &word) >= 0) {
        reasons.append(QStringLiteral("not the item itself (\"%1\")").arg(word));
        return done(Bucket::NoMatch);
    }
    if (firstPhrase(list, words.lot, &word) >= 0) {
        reasons.append(QStringLiteral("\"%1\" = lot word -> lot").arg(word));
        return done(Bucket::Lot);
    }
    if (firstNotWithItem(list, words.documentation, words, &word) >= 0) {
        reasons.append(QStringLiteral("\"%1\" = documentation word, not after a with-word -> documentation").arg(word));
        return done(Bucket::Documentation);
    }
    // Parts before kits: "compatible ... feet set" is a part (S11).
    if (firstPhrase(list, words.partFits, &word) >= 0) {
        reasons.append(QStringLiteral("\"%1\" = part/fits word -> part or fits").arg(word));
        return done(Bucket::PartFits);
    }
    // Part-only words, chips, revisions and board locations: a part, unless
    // a system title names the system itself (R10: "computer, new SID chip").
    const bool named = system && itemNamed(list, words);
    if (!named && firstNotWithItem(list, words.partOnly, words, &word) >= 0) {
        reasons.append(QStringLiteral("\"%1\" = part word, not after a with-word -> part").arg(word));
        return done(Bucket::PartFits);
    }
    if (!named) {
        for (const QString &listWord : list) {
            if (matchesAnyPattern(listWord, words.partPatterns)) {
                reasons.append(QStringLiteral("part (chip, board or revision \"%1\")").arg(listWord));
                return done(Bucket::PartFits);
            }
        }
    }

    // Kits (owner case 9): a parts or repair kit for the model is a part;
    // the brand's own complete kit is the item; any other kit is unsure. A
    // kit word after a with-item word belongs to the item ("with NOS tube
    // set", S10).
    // A kit word right after an accessory word names the accessory ("probe
    // set", "test lead kit"): the accessory check below decides.
    QString kit;
    QStringList accessoryWords = words.accessory;
    if (system)
        accessoryWords.append(words.systemAccessory);
    bool kitFound = false;
    for (const QString &phrase : words.kitWords) {
        for (const qsizetype at : phrasePositions(list, phrase)) {
            bool withItem = false;
            for (qsizetype k = 0; k < at && !withItem; ++k)
                withItem = words.withItem.contains(list.at(k), Qt::CaseInsensitive);
            const bool afterAccessory = at > 0 && accessoryWords.contains(list.at(at - 1), Qt::CaseInsensitive);
            if (!withItem && !afterAccessory && !kitFound) {
                kitFound = true;
                kit = phrase;
            }
        }
    }
    if (kitFound) {
        if (firstPhrase(list, words.kitParts, &word) >= 0) {
            reasons.append(QStringLiteral("parts kit (\"%1\" ... \"%2\")").arg(word, kit));
            return done(Bucket::PartFits);
        }
        if (firstPhrase(list, words.kitFor, &word) >= 0) {
            reasons.append(QStringLiteral("a kit for something (\"%1\")").arg(word));
            return done(Bucket::PartFits);
        }
        if (firstPhrase(list, words.completeKit, &word) < 0) {
            reasons.append(QStringLiteral("\"%1\": the complete kit or parts for it? check").arg(kit));
            return done(Bucket::ToCheck);
        }
        reasons.append(QStringLiteral("the brand's complete kit (\"%1\" %2)").arg(word, kit));
    }

    // "... for <model>": a companion word followed within reach by the model
    // (brand names may stand between).
    const QStringList others = otherModelCodes(codeList, identity, words, models.first().at);
    for (const QString &companion : words.companion) {
        for (const qsizetype at : phrasePositions(codeList, companion)) {
            const bool forModel = std::any_of(models.cbegin(), models.cend(), [at, &brands, &codeList, &words](const Span &span) {
                if (span.at <= at || span.at - at > kForReach + 2)
                    return false;
                // Only brand names and filler words in between ("from
                // working Tektronix 1910").
                for (qsizetype k = at + 1; k < span.at; ++k) {
                    if (!brands.contains(k) && !words.companionFillers.contains(codeList.at(k), Qt::CaseInsensitive))
                        return false;
                }
                return true;
            });
            if (!forModel)
                continue;
            const QStringList before = codeList.mid(0, at);
            const QStringList beforeList = listWords(before.join(QLatin1Char(' ')));
            const QStringList otherBefore = otherModelCodes(before, identity, words, -1);
            // "<thing> from <model>" is taken from the item: a part (or an
            // accessory, below), never a product made for it - a code in
            // front is its part number.
            const bool takenFrom = companion.compare(QLatin1String("from"), Qt::CaseInsensitive) == 0;
            if (!otherBefore.isEmpty() && !takenFrom) {
                reasons.append(QStringLiteral("companion: %1 ... \"%2\" %3").arg(otherBefore.first(), companion, identity.model));
                return done(Bucket::Companion);
            }
            if (firstPhrase(beforeList, words.accessory, &word) >= 0
                || (system && firstPhrase(beforeList, words.systemAccessory, &word) >= 0)) {
                reasons.append(QStringLiteral("accessory (\"%1\" ... \"%2\" %3)").arg(word, companion, identity.model));
                return done(Bucket::Accessory);
            }
            reasons.append(QStringLiteral("\"%1\" %2: %3 -> part or fits")
                               .arg(companion, identity.model, takenFrom ? QStringLiteral("taken from the item") : QStringLiteral("made for it")));
            return done(Bucket::PartFits);
        }
    }
    if (system && !named && firstNotWithItem(list, words.media, words, &word) >= 0) {
        reasons.append(QStringLiteral("software or media for the system (\"%1\")").arg(word));
        return done(Bucket::Companion);
    }
    if (system) {
        QList<qsizetype> modelPositions;
        for (const Span &span : models) {
            for (qsizetype k = 0; k < span.length; ++k)
                modelPositions.append(span.at + k);
        }
        const QString other = otherSystemIn(masked(codeList, modelPositions), identity, words);
        if (!other.isEmpty()) {
            reasons.append(QStringLiteral("also names %1: for several systems").arg(other));
            return done(Bucket::Accessory);
        }
    }
    if (!others.isEmpty() && firstPhrase(list, words.partWords, &word) >= 0) {
        reasons.append(QStringLiteral("part (\"%1\" with other models: %2)").arg(word, others.join(QStringLiteral(", "))));
        return done(Bucket::PartFits);
    }
    if (others.size() >= 2) {
        reasons.append(QStringLiteral("several other models: %1").arg(others.join(QStringLiteral(", "))));
        return done(Bucket::PartFits);
    }
    QStringList accessories = words.accessory;
    if (system && !named)
        accessories.append(words.systemAccessory);
    if (firstNotWithItem(list, accessories, words, &word) >= 0) {
        reasons.append(QStringLiteral("\"%1\" = accessory word, not after a with-word -> accessory").arg(word));
        return done(Bucket::Accessory);
    }
    if (const QString other = joinedOtherModel(codeList, models, others, words); !other.isEmpty()) {
        reasons.append(QStringLiteral("\"%1\" joined to %2 by a bundle word: two items -> lot").arg(other, identity.model));
        return done(Bucket::Lot);
    }
    if (system) {
        if (const QString reissue = reissueNextToModel(codeList, models, words); !reissue.isEmpty()) {
            reasons.append(QStringLiteral("another model: a re-issue (\"%1\")").arg(reissue));
            return done(Bucket::NoMatch);
        }
        if (firstPhrase(list, words.merchandise, &word) >= 0) {
            reasons.append(QStringLiteral("merchandise (\"%1\"), not the item").arg(word));
            return done(Bucket::NoMatch);
        }
    }
    // Every word was looked at: say what came with the item.
    QStringList withWords = withItemPhrases(list, accessories, words);
    withWords.append(withItemPhrases(list, words.documentation, words));
    withWords.append(withItemPhrases(list, words.partOnly, words));
    if (!withWords.isEmpty())
        reasons.append(QStringLiteral("%1: comes with the item").arg(withWords.join(QStringLiteral(", "))));
    reasons.append(QStringLiteral("no lot, documentation, part, accessory or other-model word for something else -> same item"));
    return done(Bucket::SameItem);
}

namespace {

// The description's sentences (split at . ! ? ; : , and line breaks), each as
// listWords; "n't" reads as " not" and a possessive "'s" is dropped first, so
// "doesn't work" is "does not work" and "the unit's stand" is "the unit stand".
QList<QStringList> descriptionClauses(const QString &description)
{
    QString text = description;
    text.replace(QChar(0x2019), QLatin1Char('\''));
    static const QRegularExpression notForm(QStringLiteral("n't\\b"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression possessive(QStringLiteral("'s\\b"), QRegularExpression::CaseInsensitiveOption);
    text.replace(notForm, QStringLiteral(" not"));
    text.replace(possessive, QString());
    static const QRegularExpression breaks(QStringLiteral("[.!?;:,\\n\\r]+"));
    QList<QStringList> clauses;
    for (const QString &part : text.split(breaks, Qt::SkipEmptyParts)) {
        const QStringList clause = listWords(part);
        if (!clause.isEmpty())
            clauses.append(clause);
    }
    return clauses;
}

// Positions of `phrase` in `clause`, the phrase split the same way as the
// clause (a model with "/" in it matches too).
QList<qsizetype> clausePositions(const QStringList &clause, const QString &phrase)
{
    return phrasePositions(clause, listWords(phrase).join(QLatin1Char(' ')));
}

// A sentence of terms and returns ("refunds if not working as described"):
// its words say nothing about this item.
bool policyClause(const QStringList &clause)
{
    static const QStringList policy{QStringLiteral("if"), QStringLiteral("unless"), QStringLiteral("return"),
                                    QStringLiteral("returns"), QStringLiteral("refund"), QStringLiteral("refunds")};
    return std::any_of(clause.cbegin(), clause.cend(), [](const QString &word) { return policy.contains(word); });
}

} // namespace

DescriptionFinding descriptionFinding(const QString &description, const Identity &identity, const MatchWords &words)
{
    DescriptionFinding finding;
    const QList<QStringList> clauses = descriptionClauses(description);
    if (clauses.isEmpty())
        return finding;
    auto found = [&finding](Bucket bucket, const QString &phrase, const QString &what) {
        finding.bucket = bucket;
        finding.reason = QStringLiteral("description: \"%1\" - %2").arg(phrase, what);
    };
    // "<word> only" ending its sentence ("Leads only."). Not "the case only
    // has scratches", not "only the case is scratched".
    const auto onlyPhrase = [&clauses](const QStringList &phrases, QString *which) {
        for (const QStringList &clause : clauses) {
            if (clause.size() < 2 || clause.last() != QLatin1String("only"))
                continue;
            for (const QString &phrase : phrases) {
                const QStringList parts = listWords(phrase);
                if (parts.isEmpty() || parts.size() + 1 > clause.size())
                    continue;
                if (clause.mid(clause.size() - 1 - parts.size(), parts.size()) == parts) {
                    *which = phrase + QStringLiteral(" only");
                    return true;
                }
            }
        }
        return false;
    };
    QString which;
    QStringList accessories = words.accessory;
    if (isSystem(identity, words))
        accessories.append(words.systemAccessory);
    if (onlyPhrase(accessories, &which)) {
        found(Bucket::Accessory, which, QStringLiteral("accessory"));
        return finding;
    }
    if (onlyPhrase(words.documentation, &which)) {
        found(Bucket::Documentation, which, QStringLiteral("documentation"));
        return finding;
    }
    QStringList parts = words.partOnly;
    parts.append(words.partWords);
    if (onlyPhrase(parts, &which)) {
        found(Bucket::PartFits, which, QStringLiteral("part"));
        return finding;
    }
    // The item itself is missing: "<noun> (is) not included" with the noun
    // starting its sentence or after "the" / "main" ("The unit is not
    // included", not "power supply for this unit not included"); "does not
    // include the <noun>" / "without the <noun>" ending their sentence.
    QStringList nouns = words.descriptionItemNouns;
    if (!identity.model.isEmpty())
        nouns.append(identity.model.toLower());
    for (const QStringList &clause : clauses) {
        if (policyClause(clause))
            continue;
        for (const QString &noun : std::as_const(nouns)) {
            const qsizetype length = listWords(noun).size();
            for (const QString &tail : {QStringLiteral(" not included"), QStringLiteral(" is not included")}) {
                for (const qsizetype at : clausePositions(clause, noun + tail)) {
                    const bool starts = at == 0 || clause.at(at - 1) == QLatin1String("the") || clause.at(at - 1) == QLatin1String("main");
                    if (starts) {
                        found(Bucket::Accessory, noun + tail, QStringLiteral("the item itself is not included"));
                        return finding;
                    }
                }
            }
            for (const QString &head : {QStringLiteral("does not include the "), QStringLiteral("without the ")}) {
                const qsizetype headLength = listWords(head).size();
                for (const qsizetype at : clausePositions(clause, head + noun)) {
                    if (at + headLength + length == clause.size()) {
                        found(Bucket::Accessory, head + noun, QStringLiteral("the item itself is not included"));
                        return finding;
                    }
                }
            }
        }
    }
    for (const QStringList &clause : clauses) {
        for (const QString &phrase : words.descriptionLot) {
            if (!clausePositions(clause, phrase).isEmpty()) {
                found(Bucket::Lot, phrase, QStringLiteral("lot"));
                return finding;
            }
        }
    }
    // Sold for parts; not after "not"/"no" ("not for parts"), not in a terms
    // sentence.
    for (const QStringList &clause : clauses) {
        if (policyClause(clause))
            continue;
        for (const QString &phrase : words.descriptionParts) {
            for (const qsizetype at : clausePositions(clause, phrase)) {
                if (at > 0 && (clause.at(at - 1) == QLatin1String("not") || clause.at(at - 1) == QLatin1String("no")))
                    continue;
                found(Bucket::PartFits, phrase, QStringLiteral("sold for parts"));
                finding.forParts = true;
                return finding;
            }
        }
    }
    for (const QStringList &clause : clauses) {
        if (policyClause(clause))
            continue;
        for (const QString &phrase : words.descriptionUntested) {
            if (!clausePositions(clause, phrase).isEmpty()) {
                finding.note = QStringLiteral("description: \"%1\"").arg(phrase);
                return finding;
            }
        }
    }
    return finding;
}

QString likelyBrokenReason(const QString &title, const QString &ebayCondition, const MatchWords &words)
{
    const QString condition = ebayCondition.simplified();
    for (const QString &parts : words.partsConditions) {
        if (condition.compare(parts, Qt::CaseInsensitive) == 0)
            return QStringLiteral("eBay says '%1'").arg(condition);
    }
    // Both word forms: "parts/repair" is one word before the split at "/".
    const QStringList list = listWords(title);
    const QStringList whole = titleWords(title.toLower());
    for (const QString &phrase : words.likelyBroken) {
        if (!phrasePositions(list, phrase).isEmpty() || !phrasePositions(whole, phrase).isEmpty())
            return QStringLiteral("the title says '%1'").arg(phrase);
    }
    return {};
}

QString bucketName(Bucket bucket)
{
    switch (bucket) {
    case Bucket::SameItem:
        return QStringLiteral("same item");
    case Bucket::Documentation:
        return QStringLiteral("documentation");
    case Bucket::PartFits:
        return QStringLiteral("part/fits");
    case Bucket::Companion:
        return QStringLiteral("companion");
    case Bucket::Accessory:
        return QStringLiteral("accessory");
    case Bucket::Lot:
        return QStringLiteral("lot");
    case Bucket::UnknownItem:
        return QStringLiteral("unknown item");
    case Bucket::ToCheck:
        return QStringLiteral("to check");
    case Bucket::NoMatch:
        break;
    }
    return QStringLiteral("not this item");
}

} // namespace app::lookup
