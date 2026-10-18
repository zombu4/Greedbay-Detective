#pragma once

#include "db/Migrations.h"

#include <QDateTime>
#include <QString>

#include <functional>

namespace app {

// Opens the SQLite database and brings its schema to the latest version
// (PLAN section 4.2).
//
// The schema version is PRAGMA user_version. Opening:
//   - equal to the latest version: opened unchanged;
//   - higher (made by a newer app): refused, file untouched;
//   - lower: an existing file is first copied with VACUUM INTO to
//     backups/<appIdentifier>-v<old>-<UTC time>.sqlite (when at least twice its
//     size is free), then each pending migration runs in its
//     own transaction with foreign keys off, sets user_version inside that
//     transaction, and must pass PRAGMA foreign_key_check before COMMIT.
//     Afterwards PRAGMA integrity_check must return "ok". On any failure the
//     original content is restored from the backup by an atomic file replace
//     (a database the call itself created is deleted instead), so the app
//     never runs on a half-migrated database and the file never disappears.
//     After every upgrade attempt only the newest Options::keepBackups backups
//     are kept; the backup made by the call is never deleted.
// Every connection has foreign keys on and temp_store in memory, so SQLite
// writes no temporary files outside the database's own journal.
//
// Ownership: every migration stamps PRAGMA application_id = kApplicationId.
// An existing file with tables but a different application_id was not made by
// this app and is refused, untouched (outcome NotOurs). A missing database
// next to existing backups is refused too (MissingWithBackups), so a lost file
// never turns silently into an empty history; the caller decides what to do.
enum class DatabaseOutcome { Opened, Created, Upgraded, RefusedNewer, NotOurs, MissingWithBackups, Failed };

struct DatabaseResult {
    DatabaseOutcome outcome = DatabaseOutcome::Failed;
    int fromVersion = 0;
    int toVersion = 0;
    QString backupPath;  // the backup made by this call, if any
    QString error;       // why it was refused or failed
    QString warning;     // non-fatal problem, e.g. backup rotation failed
    bool ok() const
    {
        return outcome == DatabaseOutcome::Opened || outcome == DatabaseOutcome::Created
               || outcome == DatabaseOutcome::Upgraded;
    }
};

struct DatabaseOptions {
    int keepBackups = 5;
    // Free bytes at a path. Empty means QStorageInfo; tests replace it.
    std::function<qint64(const QString &)> freeBytes;
    // Called after each committed migration with its version.
    std::function<void(int)> afterMigration;
};

class Database {
public:
    // Fixed PRAGMA application_id of this app's databases. An arbitrary
    // constant; it must never change, or existing databases are refused.
    static constexpr int kApplicationId = 0x5A3C91E7;

    using Outcome = DatabaseOutcome;
    using Result = DatabaseResult;
    using Options = DatabaseOptions;

    // Opens `databasePath` as the QSQLITE connection `connectionName`. On
    // success (Opened, Created, Upgraded) the connection stays open
    // (QSqlDatabase::database(connectionName)); otherwise it is closed and
    // removed.
    static Result open(const QString &connectionName, const QString &databasePath,
                       const QString &backupsDir, const QString &appIdentifier,
                       const QList<Migration> &migrations, const Options &options = Options());

    // Closes and removes the connection.
    static void close(const QString &connectionName);

    // <appIdentifier>-v<fromVersion>-<yyyyMMdd>T<HHmmsszzz>Z.sqlite
    static QString backupFileName(const QString &appIdentifier, int fromVersion, const QDateTime &utc);

    // Deletes all but the newest `keep` (at least 1) backups of `appIdentifier` in
    // `backupsDir`, never the file named `protectedName`. Only regular files
    // whose names match backupFileName() are considered; anything else, and
    // any link, is left alone. A backups folder that is a link is refused.
    static bool rotateBackups(const QString &backupsDir, const QString &appIdentifier, int keep,
                              const QString &protectedName, QString *error);
};

} // namespace app
