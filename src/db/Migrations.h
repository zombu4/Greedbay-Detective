#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace app {

// One schema migration: running its statements takes the database from
// version - 1 to version.
struct Migration {
    int version = 0;
    QString name;
    QStringList statements;
};

// Loads the migrations shipped as Qt resources under `resourceDir`
// (":/migrations"). File names are NNNN_name.sql with NNNN the version, e.g.
// 0001_init.sql. Versions must run 1, 2, 3, … without gaps; every file must
// split into at least one statement (see splitSqlScript). Returns
// std::nullopt and sets *error otherwise.
std::optional<QList<Migration>> loadMigrations(const QString &resourceDir, QString *error);

// True when `migrations` is sorted with versions 1..N and no gaps.
bool migrationsAreContiguous(const QList<Migration> &migrations);

} // namespace app
