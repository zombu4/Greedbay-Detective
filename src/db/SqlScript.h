#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace app {

// Splits an SQL script into single statements, because QSqlQuery::exec runs
// one statement at a time (tst_sqlscript checks this against the QSQLITE
// driver in use).
//
// Statements end at ';' outside of quoted text and comments. Recognised:
// '...' strings (with '' as an escaped quote), "..." and `...` and [...]
// identifiers, -- line comments and /* */ block comments. Comments are
// dropped; empty statements are dropped.
//
// CREATE TRIGGER is refused, because a trigger body contains ';' that does
// not end the statement; migrations must not use triggers. An unterminated
// string, identifier or block comment is refused as well.
std::optional<QStringList> splitSqlScript(const QString &script, QString *error);

} // namespace app
