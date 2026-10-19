#include "lookup/Identity.h"

#include <QRegularExpression>

#include <algorithm>

namespace app::lookup {

namespace {

bool hasDigit(const QString &word)
{
    return std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isDigit(); });
}

bool hasLetter(const QString &word)
{
    return std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetter(); });
}

void appendUnique(QStringList *list, const QString &value)
{
    if (!value.isEmpty() && !list->contains(value, Qt::CaseInsensitive))
        list->append(value);
}

} // namespace

QString canonicalModel(const QString &token)
{
    QString result = token.toUpper();
    result.remove(QLatin1Char('-'));
    result.remove(QLatin1Char('.'));
    return result;
}

QStringList modelTokens(const QString &text, bool requireLetter)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,;/()\\[\\]|+:&!?\"'\\x{2018}\\x{2019}\\x{201C}\\x{201D}]+"));
    static const QRegularExpression endsInDigit(QStringLiteral("\\d$"));
    static const QRegularExpression singleCapital(QStringLiteral("^[A-Z]$"));
    const QStringList words = text.split(separators, Qt::SkipEmptyParts);
    QStringList result;
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString word = words.at(i);
        if (endsInDigit.match(word).hasMatch() && i + 1 < words.size() && singleCapital.match(words.at(i + 1)).hasMatch()) {
            word += words.at(i + 1);
            ++i;
        }
        const QString token = canonicalModel(word);
        if (!hasDigit(token) || (requireLetter && !hasLetter(token)))
            continue;
        appendUnique(&result, token);
    }
    return result;
}

Identity identify(const ItemRecord &item)
{
    Identity identity;
    identity.brand = item.specific(QStringLiteral("Brand")).simplified();
    for (const QString &token : modelTokens(item.specific(QStringLiteral("Model")), false))
        appendUnique(&identity.modelTokens, token);
    for (const QString &token : modelTokens(item.specific(QStringLiteral("MPN")), false))
        appendUnique(&identity.modelTokens, token);
    for (const QString &token : modelTokens(item.title, true))
        appendUnique(&identity.modelTokens, token);
    return identity;
}

QStringList buildQueries(const Identity &identity, const QString &title)
{
    QStringList queries;
    const QString brand = identity.brand;
    auto withBrand = [&brand](const QString &model) {
        return brand.isEmpty() ? model : brand + QLatin1Char(' ') + model;
    };
    if (identity.modelTokens.isEmpty()) {
        appendUnique(&queries, title.simplified());
        return queries;
    }
    if (!brand.isEmpty())
        appendUnique(&queries, withBrand(identity.modelTokens.at(0)));
    appendUnique(&queries, identity.modelTokens.at(0));
    if (identity.modelTokens.size() > 1)
        appendUnique(&queries, withBrand(identity.modelTokens.at(1)));
    return queries;
}

} // namespace app::lookup
