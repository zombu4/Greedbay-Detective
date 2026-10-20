#include "lookup/MatchWords.h"

#include "lookup/Text.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace app::lookup {

namespace {

bool readList(const QJsonValue &value, QStringList *out)
{
    if (!value.isArray())
        return false;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString() || entry.toString().trimmed().isEmpty())
            return false;
        out->append(entry.toString().trimmed());
    }
    return true;
}

} // namespace

std::optional<MatchWords> MatchWords::fromJson(const QByteArray &json, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    const QJsonObject root = document.object();
    if (!document.isObject() || root.value(QStringLiteral("format")).toInt() != 1) {
        *error = QStringLiteral("the word lists are not a format-1 JSON object");
        return std::nullopt;
    }
    MatchWords words;
    auto wordsOf = [&root](const char *key) { return root.value(QLatin1String(key)).toObject().value(QStringLiteral("words")); };
    auto patternsOf = [&root](const char *key) {
        return root.value(QLatin1String(key)).toObject().value(QStringLiteral("patterns"));
    };
    const struct {
        QJsonValue value;
        QStringList *list;
        const char *name;
    } lists[] = {
        {wordsOf("specUnits"), &words.specUnits, "specUnits"},
        {wordsOf("specPrefixes"), &words.specPrefixes, "specPrefixes"},
        {wordsOf("specUnitsGlued"), &words.specUnitsGlued, "specUnitsGlued"},
        {wordsOf("specUnitsInPairs"), &words.specUnitsInPairs, "specUnitsInPairs"},
        {patternsOf("notModels"), &words.notModels, "notModels"},
        {wordsOf("optionWords"), &words.optionWords, "optionWords"},
        {wordsOf("modelWords"), &words.modelWords, "modelWords"},
        {wordsOf("notCodePrefixes"), &words.notCodePrefixes, "notCodePrefixes"},
        {wordsOf("tubeTypes"), &words.tubeTypes, "tubeTypes"},
        {wordsOf("notBrands"), &words.notBrands, "notBrands"},
        {wordsOf("lot"), &words.lot, "lot"},
        {wordsOf("documentation"), &words.documentation, "documentation"},
        {wordsOf("partFits"), &words.partFits, "partFits"},
        {wordsOf("partWords"), &words.partWords, "partWords"},
        {wordsOf("partOnly"), &words.partOnly, "partOnly"},
        {patternsOf("partPatterns"), &words.partPatterns, "partPatterns"},
        {wordsOf("kitWords"), &words.kitWords, "kitWords"},
        {wordsOf("kitParts"), &words.kitParts, "kitParts"},
        {wordsOf("kitFor"), &words.kitFor, "kitFor"},
        {wordsOf("completeKit"), &words.completeKit, "completeKit"},
        {wordsOf("likelyBroken"), &words.likelyBroken, "likelyBroken"},
        {wordsOf("partsConditions"), &words.partsConditions, "partsConditions"},
        {wordsOf("accessory"), &words.accessory, "accessory"},
        {wordsOf("notAccessory"), &words.notAccessory, "notAccessory"},
        {wordsOf("withItem"), &words.withItem, "withItem"},
        {wordsOf("companion"), &words.companion, "companion"},
        {wordsOf("merchandise"), &words.merchandise, "merchandise"},
        {wordsOf("systemCategories"), &words.systemCategories, "systemCategories"},
        {wordsOf("itemNouns"), &words.itemNouns, "itemNouns"},
        {wordsOf("media"), &words.media, "media"},
        {wordsOf("systemAccessory"), &words.systemAccessory, "systemAccessory"},
        {wordsOf("reissue"), &words.reissue, "reissue"},
    };
    for (const auto &entry : lists) {
        if (!readList(entry.value, entry.list)) {
            *error = QStringLiteral("the word list %1 is missing or not a list of words").arg(QLatin1String(entry.name));
            return std::nullopt;
        }
    }
    for (const QStringList *patterns : {&words.notModels, &words.partPatterns}) {
        for (const QString &pattern : *patterns) {
            if (!QRegularExpression(pattern).isValid()) {
                *error = QStringLiteral("the pattern %1 is not a valid regular expression").arg(pattern);
                return std::nullopt;
            }
        }
    }
    for (const QString &type : std::as_const(words.tubeTypes))
        words.tubeCodes.insert(normalizeCode(type));
    return words;
}

std::optional<MatchWords> MatchWords::builtIn(QString *error)
{
    QFile file(QStringLiteral(":/lookup/match-words.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("the built-in word lists are missing");
        return std::nullopt;
    }
    return fromJson(file.readAll(), error);
}

} // namespace app::lookup
