#include "db/Database.h"

#include "core/PathChecks.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace app {

namespace {

// Files SQLite keeps next to a database: the rollback journal and, if WAL was
// ever enabled, the write-ahead log and its shared-memory index.
const char *const kSidecarSuffixes[] = {"-journal", "-wal", "-shm"};

bool exec(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        *error = QStringLiteral("%1: %2").arg(sql.left(120), query.lastError().text());
        return false;
    }
    return true;
}

std::optional<int> pragmaInt(QSqlDatabase &db, const QString &pragma, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA ") + pragma) || !query.next()) {
        *error = QStringLiteral("PRAGMA %1 failed: %2").arg(pragma, query.lastError().text());
        return std::nullopt;
    }
    return query.value(0).toInt();
}

std::optional<int> tableCount(QSqlDatabase &db, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT count(*) FROM sqlite_master")) || !query.next()) {
        *error = QStringLiteral("cannot read the schema: %1").arg(query.lastError().text());
        return std::nullopt;
    }
    return query.value(0).toInt();
}

bool foreignKeysClean(QSqlDatabase &db, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) {
        *error = QStringLiteral("PRAGMA foreign_key_check failed: %1").arg(query.lastError().text());
        return false;
    }
    if (query.next()) {
        *error = QStringLiteral("foreign key violation in table %1").arg(query.value(0).toString());
        return false;
    }
    return true;
}

bool integrityOk(QSqlDatabase &db, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA integrity_check")) || !query.next()) {
        *error = QStringLiteral("PRAGMA integrity_check failed: %1").arg(query.lastError().text());
        return false;
    }
    const QString first = query.value(0).toString();
    if (first != QLatin1String("ok") || query.next()) {
        *error = QStringLiteral("integrity check failed: %1").arg(first);
        return false;
    }
    return true;
}

QString sqlStringLiteral(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QRegularExpression backupNamePattern(const QString &appName)
{
    return QRegularExpression(QStringLiteral("^%1-v\\d+-(\\d{8}T\\d{9}Z)\\.sqlite$")
                                  .arg(QRegularExpression::escape(appName)));
}

bool hasBackups(const QString &backupsDir, const QString &appName)
{
    if (isLink(backupsDir) || !QFileInfo(backupsDir).isDir())
        return false;
    const QRegularExpression pattern = backupNamePattern(appName);
    const QDir dir(backupsDir);
    const QStringList names = dir.entryList(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    return std::any_of(names.begin(), names.end(), [&](const QString &name) {
        return pattern.match(name).hasMatch() && isPlainFile(dir.filePath(name));
    });
}

// Deletes the files SQLite keeps next to the database. Refuses to touch
// anything that is not a regular file.
bool deleteSidecars(const QString &databasePath, QString *error)
{
    for (const char *suffix : kSidecarSuffixes) {
        const QString path = databasePath + QLatin1String(suffix);
        if (!somethingAt(path))
            continue;
        if (!isPlainFile(path) || !QFile::remove(path)) {
            *error = QStringLiteral("cannot delete %1").arg(path);
            return false;
        }
    }
    return true;
}

// Deletes a database the open call itself created.
bool deleteCreatedDatabase(const QString &databasePath, QString *error)
{
    if (somethingAt(databasePath) && (!isPlainFile(databasePath) || !QFile::remove(databasePath))) {
        *error = QStringLiteral("cannot delete %1").arg(databasePath);
        return false;
    }
    return deleteSidecars(databasePath, error);
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

// Puts the backup's content in place of the database with one atomic rename
// (QSaveFile): at every moment either the old file or the restored file
// exists. The sidecar files of the failed state are removed before the
// rename, so SQLite cannot apply them to the restored content.
bool restoreFromBackup(const QString &backupPath, const QString &databasePath, QString *error)
{
    if (!isPlainFile(backupPath)) {
        *error = QStringLiteral("the backup %1 is missing").arg(backupPath);
        return false;
    }
    QFile source(backupPath);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read the backup: %1").arg(source.errorString());
        return false;
    }
    QSaveFile target(databasePath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("cannot write the restored database: %1").arg(target.errorString());
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1 << 20);
        if (chunk.isEmpty() || target.write(chunk) != chunk.size()) {
            target.cancelWriting();
            *error = QStringLiteral("cannot copy the backup: %1").arg(target.errorString());
            return false;
        }
    }
    if (!deleteSidecars(databasePath, error)) {
        target.cancelWriting();
        return false;
    }
    if (!target.commit()) {
        *error = QStringLiteral("cannot replace the database: %1").arg(target.errorString());
        return false;
    }
    if (fileSha256(databasePath) != fileSha256(backupPath)) {
        *error = QStringLiteral("the restored database differs from the backup");
        return false;
    }
    return true;
}

void dropConnection(const QString &connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isValid())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

// What the outer function must do after the connection is closed.
enum class Cleanup { None, DeleteCreated, RestoreBackup };

struct Attempt {
    Database::Result result;
    Cleanup cleanup = Cleanup::None;
};

Attempt migrateOpenDatabase(QSqlDatabase &db, const QString &databasePath, const QString &backupsDir,
                            const QString &appName, const QList<Migration> &migrations,
                            const Database::Options &options, bool existed)
{
    Attempt attempt;
    Database::Result &result = attempt.result;
    // Before a backup exists a failure must leave an existing file untouched;
    // after it, the backup is restored. A file this call created is deleted.
    const Cleanup beforeBackup = existed ? Cleanup::None : Cleanup::DeleteCreated;
    const Cleanup afterBackup = existed ? Cleanup::RestoreBackup : Cleanup::DeleteCreated;
    auto fail = [&](Cleanup cleanup) {
        result.outcome = Database::Outcome::Failed;
        attempt.cleanup = cleanup;
        return attempt;
    };

    if (!exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), &result.error))
        return fail(beforeBackup);
    std::optional<int> value = pragmaInt(db, QStringLiteral("foreign_keys"), &result.error);
    if (!value)
        return fail(beforeBackup);
    if (*value != 1) {
        result.error = QStringLiteral("this SQLite build does not enforce foreign keys");
        return fail(beforeBackup);
    }
    if (!exec(db, QStringLiteral("PRAGMA temp_store = MEMORY"), &result.error))
        return fail(beforeBackup);
    value = pragmaInt(db, QStringLiteral("temp_store"), &result.error);
    if (!value)
        return fail(beforeBackup);
    if (*value != 2) {
        result.error = QStringLiteral("PRAGMA temp_store did not switch to memory");
        return fail(beforeBackup);
    }

    const std::optional<int> applicationId = pragmaInt(db, QStringLiteral("application_id"), &result.error);
    const std::optional<int> objects = tableCount(db, &result.error);
    value = pragmaInt(db, QStringLiteral("user_version"), &result.error);
    if (!applicationId || !objects || !value)
        return fail(beforeBackup);
    const int current = *value;
    const bool emptyFile = *objects == 0 && current == 0 && *applicationId == 0;
    if (*applicationId != Database::kApplicationId && !emptyFile) {
        result.outcome = Database::Outcome::NotOurs;
        result.error = QStringLiteral("the database file was not created by this app "
                                      "(application_id %1); it was not changed")
                           .arg(*applicationId);
        attempt.cleanup = Cleanup::None;
        return attempt;
    }

    const int latest = migrations.last().version;
    result.fromVersion = current;
    result.toVersion = current;

    if (current == latest) {
        result.outcome = Database::Outcome::Opened;
        return attempt;
    }
    if (current > latest) {
        result.outcome = Database::Outcome::RefusedNewer;
        result.error = QStringLiteral("the database has schema version %1, newer than this app's %2; "
                                      "it was not changed")
                           .arg(current)
                           .arg(latest);
        attempt.cleanup = Cleanup::None;
        return attempt;
    }

    if (existed) {
        if (isLink(backupsDir) || !QDir().mkpath(backupsDir)) {
            result.error = QStringLiteral("the backups folder is not usable");
            return fail(Cleanup::None);
        }
        qint64 size = QFileInfo(databasePath).size();
        for (const char *suffix : kSidecarSuffixes)
            size += QFileInfo(databasePath + QLatin1String(suffix)).size();
        const qint64 free = options.freeBytes ? options.freeBytes(backupsDir)
                                              : QStorageInfo(backupsDir).bytesAvailable();
        if (free < 2 * size) {
            result.error = QStringLiteral("not enough free disk space for the upgrade backup "
                                          "(need %1 bytes, %2 free)")
                               .arg(2 * size)
                               .arg(free);
            return fail(Cleanup::None);
        }
        const QString backup = QDir(backupsDir).filePath(
            Database::backupFileName(appName, current, QDateTime::currentDateTimeUtc()));
        if (somethingAt(backup)) {
            result.error = QStringLiteral("backup %1 already exists").arg(backup);
            return fail(Cleanup::None);
        }
        if (!exec(db, QStringLiteral("VACUUM INTO ") + sqlStringLiteral(backup), &result.error)) {
            // VACUUM INTO may leave a partial file; the name was free before, so it is ours.
            if (isPlainFile(backup))
                QFile::remove(backup);
            return fail(Cleanup::None);
        }
        if (!isPlainFile(backup)) {
            result.error = QStringLiteral("the backup was not written");
            return fail(Cleanup::None);
        }
        result.backupPath = backup;
    }

    for (const Migration &migration : migrations) {
        if (migration.version <= current)
            continue;
        // Foreign keys must be off while a migration rebuilds tables (step 1 of
        // SQLite's 12-step ALTER TABLE procedure); the pragma has no effect
        // inside a transaction, so it is set before BEGIN.
        if (!exec(db, QStringLiteral("PRAGMA foreign_keys = OFF"), &result.error))
            return fail(afterBackup);
        if (!exec(db, QStringLiteral("BEGIN IMMEDIATE"), &result.error))
            return fail(afterBackup);
        QString error;
        bool ok = true;
        for (const QString &statement : migration.statements) {
            if (!exec(db, statement, &error)) {
                ok = false;
                break;
            }
        }
        if (ok)
            ok = exec(db, QStringLiteral("PRAGMA application_id = %1").arg(Database::kApplicationId), &error);
        if (ok)
            ok = exec(db, QStringLiteral("PRAGMA user_version = %1").arg(migration.version), &error);
        if (ok)
            ok = foreignKeysClean(db, &error);
        if (ok)
            ok = exec(db, QStringLiteral("COMMIT"), &error);
        if (!ok) {
            QString ignored;
            exec(db, QStringLiteral("ROLLBACK"), &ignored);
            result.error = QStringLiteral("migration %1 (%2) failed: %3")
                               .arg(migration.version)
                               .arg(migration.name, error);
            return fail(afterBackup);
        }
        if (!exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), &result.error))
            return fail(afterBackup);
        result.toVersion = migration.version;
        if (options.afterMigration)
            options.afterMigration(migration.version);
    }

    if (!integrityOk(db, &result.error) || !foreignKeysClean(db, &result.error))
        return fail(afterBackup);
    value = pragmaInt(db, QStringLiteral("user_version"), &result.error);
    if (!value || *value != latest) {
        if (value)
            result.error = QStringLiteral("user_version is %1 after the upgrade, expected %2")
                               .arg(*value)
                               .arg(latest);
        return fail(afterBackup);
    }
    result.outcome = existed ? Database::Outcome::Upgraded : Database::Outcome::Created;
    return attempt;
}

} // namespace

QString Database::backupFileName(const QString &appName, int fromVersion, const QDateTime &utc)
{
    return QStringLiteral("%1-v%2-%3.sqlite")
        .arg(appName)
        .arg(fromVersion)
        .arg(utc.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")));
}

bool Database::rotateBackups(const QString &backupsDir, const QString &appName, int keep,
                             const QString &protectedName, QString *error)
{
    if (isLink(backupsDir)) {
        if (error)
            *error = QStringLiteral("the backups folder is a link; nothing was deleted");
        return false;
    }
    const QRegularExpression pattern = backupNamePattern(appName);
    const QDir dir(backupsDir);
    std::vector<std::pair<QString, QString>> backups; // (timestamp, file name)
    const QStringList names = dir.entryList(QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDir::NoSort);
    for (const QString &name : names) {
        const QRegularExpressionMatch match = pattern.match(name);
        if (match.hasMatch() && name != protectedName && isPlainFile(dir.filePath(name)))
            backups.emplace_back(match.captured(1), name);
    }
    std::sort(backups.begin(), backups.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second > b.second;
    });
    // The protected backup counts as one of the kept ones.
    const int keepOthers = std::max(keep, 1) - (protectedName.isEmpty() ? 0 : 1);
    bool ok = true;
    for (std::size_t i = static_cast<std::size_t>(std::max(keepOthers, 0)); i < backups.size(); ++i) {
        if (!QFile::remove(dir.filePath(backups[i].second))) {
            ok = false;
            if (error)
                *error = QStringLiteral("cannot delete old backup %1").arg(backups[i].second);
        }
    }
    return ok;
}

Database::Result Database::open(const QString &connectionName, const QString &databasePath,
                                const QString &backupsDir, const QString &appName,
                                const QList<Migration> &migrations, const Options &options)
{
    Result result;
    if (migrations.isEmpty() || !migrationsAreContiguous(migrations)) {
        result.error = QStringLiteral("the migration list is empty or has gaps");
        return result;
    }
    if (QSqlDatabase::contains(connectionName)) {
        result.error = QStringLiteral("the connection %1 is already open").arg(connectionName);
        return result;
    }
    if (isLink(databasePath)) {
        result.error = QStringLiteral("the database path is a link; links are never followed");
        return result;
    }
    const bool existed = somethingAt(databasePath);
    if (existed && !isPlainFile(databasePath)) {
        result.error = QStringLiteral("the database path is not a regular file");
        return result;
    }
    if (!existed && hasBackups(backupsDir, appName)) {
        result.outcome = Outcome::MissingWithBackups;
        result.error = QStringLiteral("the database is missing but backups of it exist in %1; "
                                      "nothing was created")
                           .arg(backupsDir);
        return result;
    }

    Attempt attempt;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            attempt.result.error = QStringLiteral("cannot open the database: %1").arg(db.lastError().text());
            attempt.cleanup = existed ? Cleanup::None : Cleanup::DeleteCreated;
        } else {
            attempt = migrateOpenDatabase(db, databasePath, backupsDir, appName, migrations, options, existed);
        }
    }
    result = attempt.result;
    const QString madeBackup = QFileInfo(result.backupPath).fileName();

    if (!result.ok()) {
        dropConnection(connectionName);
        QString cleanupError;
        switch (attempt.cleanup) {
        case Cleanup::None:
            break;
        case Cleanup::DeleteCreated:
            if (!deleteCreatedDatabase(databasePath, &cleanupError))
                result.error += QStringLiteral("; cleanup failed: ") + cleanupError;
            break;
        case Cleanup::RestoreBackup:
            if (!restoreFromBackup(result.backupPath, databasePath, &cleanupError))
                result.error += QStringLiteral("; RESTORE FAILED, the database was left as the failed "
                                               "upgrade left it and the backup is kept at %1: %2")
                                    .arg(result.backupPath, cleanupError);
            break;
        }
    }
    // Keep the number of backups bounded after every upgrade attempt, also a
    // failed one; the backup this call made is never deleted.
    if (!madeBackup.isEmpty()) {
        QString rotateError;
        if (!rotateBackups(backupsDir, appName, options.keepBackups, madeBackup, &rotateError))
            result.warning = rotateError;
    }
    return result;
}

void Database::close(const QString &connectionName)
{
    dropConnection(connectionName);
}

} // namespace app
