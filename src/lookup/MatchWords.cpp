#include "lookup/MatchWords.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    const QJsonObject conditions = root.value(QStringLiteral("conditions")).toObject();
    const QJsonObject ebay = root.value(QStringLiteral("ebayConditions")).toObject();
    auto wordsOf = [&root](const char *key) { return root.value(QLatin1String(key)).toObject().value(QStringLiteral("words")); };
    const struct {
        QJsonValue value;
        QStringList *list;
        const char *name;
    } lists[] = {
        {wordsOf("specUnits"), &words.specUnits, "specUnits"},
        {wordsOf("specPrefixes"), &words.specPrefixes, "specPrefixes"},
        {wordsOf("lot"), &words.lot, "lot"},
        {wordsOf("documentation"), &words.documentation, "documentation"},
        {wordsOf("partFits"), &words.partFits, "partFits"},
        {wordsOf("partWords"), &words.partWords, "partWords"},
        {wordsOf("accessory"), &words.accessory, "accessory"},
        {wordsOf("withItem"), &words.withItem, "withItem"},
        {wordsOf("companion"), &words.companion, "companion"},
        {conditions.value(QStringLiteral("nonWorking")), &words.nonWorking, "conditions.nonWorking"},
        {conditions.value(QStringLiteral("untested")), &words.untested, "conditions.untested"},
        {conditions.value(QStringLiteral("new")), &words.newWords, "conditions.new"},
        {conditions.value(QStringLiteral("working")), &words.working, "conditions.working"},
        {ebay.value(QStringLiteral("new")), &words.ebayNew, "ebayConditions.new"},
        {ebay.value(QStringLiteral("used")), &words.ebayUsed, "ebayConditions.used"},
        {ebay.value(QStringLiteral("nonWorking")), &words.ebayNonWorking, "ebayConditions.nonWorking"},
    };
    for (const auto &entry : lists) {
        if (!readList(entry.value, entry.list)) {
            *error = QStringLiteral("the word list %1 is missing or not a list of words").arg(QLatin1String(entry.name));
            return std::nullopt;
        }
    }
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
