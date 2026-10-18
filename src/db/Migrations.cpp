#include "db/Migrations.h"

#include "db/SqlScript.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace app {

bool migrationsAreContiguous(const QList<Migration> &migrations)
{
    for (qsizetype i = 0; i < migrations.size(); ++i) {
        if (migrations.at(i).version != i + 1)
            return false;
    }
    return true;
}

std::optional<QList<Migration>> loadMigrations(const QString &resourceDir, QString *error)
{
    auto fail = [error](const QString &message) -> std::optional<QList<Migration>> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    static const QRegularExpression namePattern(QStringLiteral("^(\\d{4})_([a-z0-9_]+)\\.sql$"));

    const QDir dir(resourceDir);
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    QList<Migration> migrations;
    for (const QString &fileName : files) {
        const QRegularExpressionMatch match = namePattern.match(fileName);
        if (!match.hasMatch())
            return fail(QStringLiteral("unexpected file in migrations: %1").arg(fileName));

        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot read migration %1").arg(fileName));
        const QString script = QString::fromUtf8(file.readAll());

        QString splitError;
        const std::optional<QStringList> statements = splitSqlScript(script, &splitError);
        if (!statements)
            return fail(QStringLiteral("migration %1: %2").arg(fileName, splitError));
        if (statements->isEmpty())
            return fail(QStringLiteral("migration %1 has no statements").arg(fileName));

        migrations.append(Migration{match.captured(1).toInt(), match.captured(2), *statements});
    }
    if (migrations.isEmpty())
        return fail(QStringLiteral("no migrations found in %1").arg(resourceDir));
    if (!migrationsAreContiguous(migrations))
        return fail(QStringLiteral("migration versions must run 1, 2, 3, ... without gaps"));
    return migrations;
}

} // namespace app
