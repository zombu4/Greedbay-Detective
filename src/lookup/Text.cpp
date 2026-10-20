#include "lookup/Text.h"

#include <QRegularExpression>

#include <algorithm>

namespace app::lookup {

QString normalizeCode(const QString &code)
{
    QString result = code.toUpper();
    result.remove(QRegularExpression(QStringLiteral("[-./\\s]")));
    return result;
}

QStringList titleWords(const QString &text)
{
    static const QRegularExpression separators(
        QStringLiteral("[\\s,;()\\[\\]!?\"'#*|:\\x{2018}\\x{2019}\\x{201C}\\x{201D}]+"));
    static const QRegularExpression withSlash(QStringLiteral("^(w/)(.*)$"), QRegularExpression::CaseInsensitiveOption);
    QStringList words;
    for (QString word : text.split(separators, Qt::SkipEmptyParts)) {
        // "w/" stays a word of its own, also when glued to the next one ("w/Leads").
        const QRegularExpressionMatch with = withSlash.match(word);
        if (with.hasMatch()) {
            words.append(with.captured(1));
            word = with.captured(2);
        }
        if (word.compare(QLatin1String("p/n"), Qt::CaseInsensitive) != 0) {
            while (!word.isEmpty() && QStringLiteral("-./").contains(word.front()))
                word.remove(0, 1);
            while (!word.isEmpty() && QStringLiteral("-./").contains(word.back()))
                word.chop(1);
        }
        if (!word.isEmpty())
            words.append(word);
    }
    return words;
}

QStringList listWords(const QString &text)
{
    QStringList words;
    for (const QString &word : titleWords(text.toLower())) {
        if (word.contains(QLatin1Char('/')) && word != QLatin1String("w/") && word != QLatin1String("p/n")) {
            for (const QString &part : word.split(QLatin1Char('/'), Qt::SkipEmptyParts))
                words.append(part);
        } else {
            words.append(word);
        }
    }
    return words;
}

bool hasLetterAndDigit(const QString &word)
{
    return std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetter(); })
           && std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isDigit(); });
}

QList<qsizetype> phrasePositions(const QStringList &words, const QString &phrase)
{
    const QStringList parts = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<qsizetype> positions;
    if (parts.isEmpty())
        return positions;
    for (qsizetype i = 0; i + parts.size() <= words.size(); ++i) {
        bool all = true;
        for (qsizetype k = 0; k < parts.size() && all; ++k)
            all = words.at(i + k).compare(parts.at(k), Qt::CaseInsensitive) == 0;
        if (all)
            positions.append(i);
    }
    return positions;
}

bool isDigitsOnly(const QString &code)
{
    const QString normalized = normalizeCode(code);
    return !normalized.isEmpty()
           && std::all_of(normalized.cbegin(), normalized.cend(), [](QChar c) { return c.isDigit(); });
}

QList<qsizetype> positionsAfterNames(const QStringList &words, const QStringList &names)
{
    QList<qsizetype> positions;
    for (const QString &name : names) {
        const QStringList parts = titleWords(name);
        if (parts.isEmpty())
            continue;
        for (const qsizetype at : phrasePositions(words, parts.join(QLatin1Char(' ')))) {
            if (!positions.contains(at + parts.size()))
                positions.append(at + parts.size());
        }
    }
    return positions;
}

} // namespace app::lookup
