#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace app {

// Copies of the database outside the work folder (owner decision
// 2026-10-22, after the work folder was lost):
//   <GenericDataLocation>/<identifier>-Backups/
//     daily-YYYY-MM-DD.sqlite           one a day; the last 7 are kept
//     before-<what>-YYYYMMDD-HHMMSS.sqlite  before a migration, an uninstall
//                                       or a restore; the last 3 are kept
//     restore.txt                       the backup to put back at the next
//                                       start (scheduleRestore)
// Every copy is SQLite's own consistent copy (VACUUM INTO, an online
// backup), never a file copy of an open database. The folder is not part of
// the work folder: an uninstall that deletes the user's data removes it only
// when the user ticks that too.
class Backups {
public:
    static constexpr int kKeepDaily = 7;
    static constexpr int kKeepBeforeChange = 3;

    struct Entry {
        QString name;   // file name
        QString kind;   // "daily" or "before ..." (what it was made before)
        QDateTime made; // UTC, from the file's own time
        qint64 bytes = 0;
    };

    // The folder for `appIdentifier` (its parent is the work folder's parent).
    static QString folderFor(const QString &appIdentifier);

    explicit Backups(QString folder);
    QString folder() const { return m_folder; }

    // Today's daily copy through the open connection `connectionName`, when
    // there is none for today yet; then the old ones beyond kKeepDaily go.
    bool daily(const QString &connectionName, QString *error);

    // A copy of the database file `databasePath` (opened read-only for it)
    // before `what` ("migration", "uninstall", "restore"); then the old ones
    // beyond kKeepBeforeChange go. True also when there is no database file
    // yet (nothing to keep).
    bool beforeChange(const QString &databasePath, const QString &what, QString *error);

    // beforeChange("migration") only when the database's PRAGMA user_version
    // is below `latestVersion` (an upgrade is about to run).
    bool beforeMigration(const QString &databasePath, int latestVersion, QString *error);

    // The copies, newest first.
    QList<Entry> list() const;

    // Restore: the copy `name` replaces the database at the next start, after
    // a "before restore" copy of the current one. applyPendingRestore() runs
    // before the database is opened; true when nothing was pending or the
    // copy was put back, false and *error when it could not be.
    bool scheduleRestore(const QString &name, QString *error);
    QString pendingRestore() const;
    bool applyPendingRestore(const QString &databasePath, QString *error);

private:
    bool copyInto(const QString &connectionName, const QString &target, QString *error) const;
    void prune(const QString &prefix, int keep) const;

    QString m_folder;
};

} // namespace app
