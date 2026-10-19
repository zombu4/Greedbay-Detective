#include "lookup/Matcher.h"

#include "lookup/Text.h"

#include <algorithm>

namespace app::lookup {

namespace {

constexpr qsizetype kForReach = 3;

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

// Positions in `words` (title words) where the model or a family member
// stands, alone or as 2-3 neighbouring words written together.
QList<qsizetype> modelPositions(const QStringList &words, const Identity &identity)
{
    QStringList codes{normalizeCode(identity.model)};
    for (const QString &member : identity.family)
        codes.append(normalizeCode(member));
    QList<qsizetype> positions;
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString joined;
        for (qsizetype n = 0; n < 3 && i + n < words.size(); ++n) {
            joined += normalizeCode(words.at(i + n));
            if (codes.contains(joined)) {
                positions.append(i);
                break;
            }
        }
    }
    return positions;
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

// Model codes in the title other than the item's own (rule 14).
QStringList otherModelCodes(const QStringList &words, const Identity &identity, const MatchWords &matchWords)
{
    QStringList own{normalizeCode(identity.model)};
    for (const QString &member : identity.family)
        own.append(normalizeCode(member));
    QStringList others;
    for (qsizetype i = 0; i < words.size(); ++i) {
        if (!isModelLike(words, i, matchWords))
            continue;
        const QString code = normalizeCode(words.at(i));
        if (!own.contains(code) && !others.contains(code))
            others.append(code);
    }
    return others;
}

// The first word of `list` at a position not preceded, within three words,
// by a with-item word ("with manual", "w/ Leads"); -1 when none.
qsizetype firstNotWithItem(const QStringList &words, const QStringList &list, const MatchWords &matchWords, QString *which)
{
    for (const QString &phrase : list) {
        for (const qsizetype at : phrasePositions(words, phrase)) {
            bool withItem = false;
            for (qsizetype k = std::max<qsizetype>(0, at - 3); k < at && !withItem; ++k)
                withItem = matchWords.withItem.contains(words.at(k), Qt::CaseInsensitive);
            if (!withItem) {
                *which = phrase;
                return at;
            }
        }
    }
    return -1;
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
    const QStringList list = listWords(title);
    const QList<qsizetype> models = identity.model.isEmpty() ? QList<qsizetype>() : modelPositions(codeList, identity);
    QString brandWord;
    const QList<qsizetype> brands = brandPositions(codeList, identity, &brandWord);
    if (models.isEmpty()) {
        reasons.append(QStringLiteral("model %1 not in the title as a whole word").arg(identity.model));
        return done(Bucket::NoMatch);
    }
    // A brand name may also stand inside a slash-joined word ("HP/Agilent").
    if (brands.isEmpty()) {
        for (const QString &name : identity.matchNames) {
            if (!phrasePositions(list, titleWords(name.toLower()).join(QLatin1Char(' '))).isEmpty()) {
                brandWord = name;
                break;
            }
        }
    }
    if (brandWord.isEmpty()) {
        // The model without the brand: often another product with the same
        // code (cards, car parts), sometimes the item with a short title.
        // Shown for the user to decide, never counted by itself.
        reasons.append(QStringLiteral("model %1 found, but the brand %2 is not in the title: check")
                           .arg(codeList.at(models.first()), identity.brandName));
        return done(Bucket::ToCheck);
    }
    // A model of digits only ("2022") also stands for years and quantities:
    // it counts only right after a brand name.
    if (!hasLetterAndDigit(normalizeCode(codeList.at(models.first()))) && !identity.model.isEmpty()
        && std::none_of(models.cbegin(), models.cend(), [&brands](qsizetype at) { return brands.contains(at - 1); })) {
        reasons.append(QStringLiteral("model %1 is digits only and does not follow the brand name: check").arg(identity.model));
        return done(Bucket::ToCheck);
    }
    reasons.append(QStringLiteral("model %1 and brand %2 found").arg(codeList.at(models.first()), brandWord));

    QString word;
    if (firstPhrase(list, words.lot, &word) >= 0) {
        reasons.append(QStringLiteral("lot (\"%1\")").arg(word));
        return done(Bucket::Lot);
    }
    if (firstNotWithItem(list, words.documentation, words, &word) >= 0) {
        reasons.append(QStringLiteral("documentation (\"%1\")").arg(word));
        return done(Bucket::Documentation);
    }

    // Kits (owner case 9): a parts or repair kit for the model is a part;
    // the brand's own complete kit is the item; any other kit is unsure.
    QString kit;
    if (firstPhrase(list, words.kitWords, &kit) >= 0) {
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
    const QStringList others = otherModelCodes(codeList, identity, words);
    for (const QString &companion : words.companion) {
        for (const qsizetype at : phrasePositions(codeList, companion)) {
            const bool forModel = std::any_of(models.cbegin(), models.cend(), [at, &brands](qsizetype model) {
                if (model <= at || model - at > kForReach + 1)
                    return false;
                for (qsizetype k = at + 1; k < model; ++k) {
                    if (!brands.contains(k))
                        return false;
                }
                return true;
            });
            if (!forModel)
                continue;
            const QStringList before = codeList.mid(0, at);
            const QStringList beforeList = listWords(before.join(QLatin1Char(' ')));
            const QStringList otherBefore = otherModelCodes(before, identity, words);
            if (!otherBefore.isEmpty()) {
                reasons.append(QStringLiteral("companion: %1 ... \"%2\" %3").arg(otherBefore.first(), companion, identity.model));
                return done(Bucket::Companion);
            }
            if (firstPhrase(beforeList, words.accessory, &word) >= 0) {
                reasons.append(QStringLiteral("accessory (\"%1\" ... \"%2\" %3)").arg(word, companion, identity.model));
                return done(Bucket::Accessory);
            }
            reasons.append(QStringLiteral("part or fits (\"%1\" %2)").arg(companion, identity.model));
            return done(Bucket::PartFits);
        }
    }
    if (firstPhrase(list, words.partFits, &word) >= 0) {
        reasons.append(QStringLiteral("part or fits (\"%1\")").arg(word));
        return done(Bucket::PartFits);
    }
    if (!others.isEmpty() && firstPhrase(list, words.partWords, &word) >= 0) {
        reasons.append(QStringLiteral("part (\"%1\" with other models: %2)").arg(word, others.join(QStringLiteral(", "))));
        return done(Bucket::PartFits);
    }
    if (others.size() >= 2) {
        reasons.append(QStringLiteral("several other models: %1").arg(others.join(QStringLiteral(", "))));
        return done(Bucket::PartFits);
    }
    if (firstNotWithItem(list, words.accessory, words, &word) >= 0) {
        reasons.append(QStringLiteral("accessory (\"%1\")").arg(word));
        return done(Bucket::Accessory);
    }
    reasons.append(QStringLiteral("same item"));
    return done(Bucket::SameItem);
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
