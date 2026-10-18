#include "db/SqlScript.h"

#include <QRegularExpression>

namespace app {

namespace {

bool startsWithCreateTrigger(const QString &statement)
{
    static const QRegularExpression pattern(
        QStringLiteral("^CREATE\\s+(TEMP\\s+|TEMPORARY\\s+)?TRIGGER\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(statement).hasMatch();
}

} // namespace

std::optional<QStringList> splitSqlScript(const QString &script, QString *error)
{
    auto fail = [error](const QString &message) -> std::optional<QStringList> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    QStringList statements;
    QString current;
    const qsizetype length = script.size();
    qsizetype i = 0;

    auto finishStatement = [&]() -> bool {
        const QString statement = current.trimmed();
        current.clear();
        if (statement.isEmpty())
            return true;
        if (startsWithCreateTrigger(statement))
            return false;
        statements.append(statement);
        return true;
    };

    while (i < length) {
        const QChar c = script.at(i);
        const QChar next = i + 1 < length ? script.at(i + 1) : QChar();

        if (c == QLatin1Char('-') && next == QLatin1Char('-')) {
            // Line comment: skip to the end of the line, keep the newline.
            while (i < length && script.at(i) != QLatin1Char('\n'))
                ++i;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            const qsizetype end = script.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0)
                return fail(QStringLiteral("unterminated block comment"));
            current.append(QLatin1Char(' '));
            i = end + 2;
            continue;
        }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')
            || c == QLatin1Char('[')) {
            const QChar close = c == QLatin1Char('[') ? QLatin1Char(']') : c;
            qsizetype j = i + 1;
            bool closed = false;
            while (j < length) {
                if (script.at(j) == close) {
                    // A doubled quote inside '...', "..." or `...` is an escaped quote.
                    if (close != QLatin1Char(']') && j + 1 < length && script.at(j + 1) == close) {
                        j += 2;
                        continue;
                    }
                    closed = true;
                    break;
                }
                ++j;
            }
            if (!closed)
                return fail(QStringLiteral("unterminated quoted text"));
            current.append(script.mid(i, j - i + 1));
            i = j + 1;
            continue;
        }
        if (c == QLatin1Char(';')) {
            if (!finishStatement())
                return fail(QStringLiteral("CREATE TRIGGER is not allowed in migrations"));
            ++i;
            continue;
        }
        current.append(c);
        ++i;
    }
    if (!finishStatement())
        return fail(QStringLiteral("CREATE TRIGGER is not allowed in migrations"));
    return statements;
}

} // namespace app
