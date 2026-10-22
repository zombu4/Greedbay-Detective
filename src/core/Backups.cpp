#include "core/Backups.h"

#include "core/PathChecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

namespace app {

namespace {

const QString kRestoreFile = QStringLiteral("restore.txt");

// A name the folder may hold: daily-<date>.sqlite or before-<what>-<time>.sqlite.
bool isBackupName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(daily-\\d{4}-\\d{2}-\\d{2}|before-[a-z]+-\\d{8}-\\d{6})\\.sqlite$"));
    return pattern.match(name).hasMatch();
}

// A connection of its own to `path`, read-only, for one copy; removed again
// by the caller through the returned name.
QString openReadOnly(const QString &path, QString *error)
{
    const QString name = QStringLiteral("backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        *error = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return {};
    }
    return name;
}

void closeConnection(const QString &name)
{
    {
        QSqlDatabase db = QSqlDatabase::database(name, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

} // namespace

QString Backups::folderFor(const QString &appIdentifier)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty() || appIdentifier.isEmpty())
        return {};
    return QDir::cleanPath(base + QLatin1Char('/') + appIdentifier + QStringLiteral("-Backups"));
}

Backups::Backups(QString folder) : m_folder(QDir::cleanPath(std::move(folder))) {}

bool Backups::copyInto(const QString &connectionName, const QString &target, QString *error) const
{
    if (m_folder.isEmpty() || isLink(m_folder) || !QDir().mkpath(m_folder)) {
        *error = QStringLiteral("the backup folder %1 cannot be used").arg(QDir::toNativeSeparators(m_folder));
        return false;
    }
    if (somethingAt(target)) {
        *error = QStringLiteral("%1 exists already").arg(QDir::toNativeSeparators(target));
        return false;
    }
    // VACUUM INTO writes a consistent copy of the open database (SQLite
    // 3.27+), also while it is in use; the path is a string literal.
    QString literal = target;
    literal.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QStringLiteral("VACUUM INTO '%1'").arg(literal))) {
        *error = query.lastError().text();
        QFile::remove(target);
        return false;
    }
    return true;
}

void Backups::prune(const QString &prefix, int keep) const
{
    QStringList names;
    for (const QString &name : QDir(m_folder).entryList(QDir::Files, QDir::Name)) {
        if (name.startsWith(prefix) && isBackupName(name))
            names.append(name);
    }
    // The names sort by time; the oldest go first.
    for (qsizetype i = 0; i + keep < names.size(); ++i) {
        const QString path = QDir(m_folder).filePath(names.at(i));
        if (isPlainFile(path))
            QFile::remove(path);
    }
}

bool Backups::daily(const QString &connectionName, QString *error)
{
    const QString name = QStringLiteral("daily-%1.sqlite").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    const QString target = QDir(m_folder).filePath(name);
    if (isPlainFile(target))
        return true;
    if (!copyInto(connectionName, target, error))
        return false;
    prune(QStringLiteral("daily-"), kKeepDaily);
    return true;
}

bool Backups::beforeChange(const QString &databasePath, const QString &what, QString *error)
{
    if (!isPlainFile(databasePath))
        return true;
    static const QRegularExpression word(QStringLiteral("^[a-z]{1,20}$"));
    if (!word.match(what).hasMatch()) {
        *error = QStringLiteral("not a backup reason: %1").arg(what);
        return false;
    }
    const QString connection = openReadOnly(databasePath, error);
    if (connection.isEmpty())
        return false;
    const QString name = QStringLiteral("before-%1-%2.sqlite")
                             .arg(what, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const bool ok = copyInto(connection, QDir(m_folder).filePath(name), error);
    closeConnection(connection);
    if (ok)
        prune(QStringLiteral("before-"), kKeepBeforeChange);
    return ok;
}

bool Backups::beforeMigration(const QString &databasePath, int latestVersion, QString *error)
{
    if (!isPlainFile(databasePath))
        return true;
    const QString connection = openReadOnly(databasePath, error);
    if (connection.isEmpty())
        return false;
    int version = -1;
    {
        QSqlQuery query(QSqlDatabase::database(connection));
        if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
            version = query.value(0).toInt();
    }
    closeConnection(connection);
    if (version < 0) {
        *error = QStringLiteral("the database version cannot be read");
        return false;
    }
    return version >= latestVersion || beforeChange(databasePath, QStringLiteral("migration"), error);
}

QList<Backups::Entry> Backups::list() const
{
    QList<Entry> entries;
    for (const QFileInfo &info : QDir(m_folder).entryInfoList(QDir::Files)) {
        if (!isBackupName(info.fileName()) || isLink(info.filePath()))
            continue;
        Entry entry;
        entry.name = info.fileName();
        entry.kind = entry.name.startsWith(QLatin1String("daily-"))
                         ? QStringLiteral("daily")
                         : QStringLiteral("before ") + entry.name.section(QLatin1Char('-'), 1, 1);
        entry.made = info.lastModified(QTimeZone::UTC);
        entry.bytes = info.size();
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.made > b.made; });
    return entries;
}

bool Backups::scheduleRestore(const QString &name, QString *error)
{
    if (!isBackupName(name) || !isPlainFile(QDir(m_folder).filePath(name))) {
        *error = QStringLiteral("there is no backup %1").arg(name);
        return false;
    }
    QSaveFile file(QDir(m_folder).filePath(kRestoreFile));
    if (!file.open(QIODevice::WriteOnly) || file.write(name.toUtf8()) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString Backups::pendingRestore() const
{
    QFile file(QDir(m_folder).filePath(kRestoreFile));
    if (!isPlainFile(file.fileName()) || !file.open(QIODevice::ReadOnly))
        return {};
    const QString name = QString::fromUtf8(file.read(200)).trimmed();
    return isBackupName(name) ? name : QString();
}

bool Backups::applyPendingRestore(const QString &databasePath, QString *error)
{
    const QString marker = QDir(m_folder).filePath(kRestoreFile);
    if (!somethingAt(marker))
        return true;
    const QString name = pendingRestore();
    // The marker goes first: a restore that fails is not tried at every start.
    QFile::remove(marker);
    const QString source = QDir(m_folder).filePath(name);
    if (name.isEmpty() || !isPlainFile(source)) {
        *error = QStringLiteral("the backup to restore is not there");
        return false;
    }
    if (!beforeChange(databasePath, QStringLiteral("restore"), error))
        return false;
    // The database is closed (nothing opened it yet): its file and its side
    // files are replaced by a consistent copy of the backup.
    for (const QString &side : {QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")}) {
        if (isPlainFile(databasePath + side) && !QFile::remove(databasePath + side)) {
            *error = QStringLiteral("%1 cannot be replaced").arg(QDir::toNativeSeparators(databasePath + side));
            return false;
        }
    }
    const QString connection = openReadOnly(source, error);
    if (connection.isEmpty())
        return false;
    const QString fresh = databasePath + QStringLiteral(".restoring");
    QFile::remove(fresh);
    QString literal = fresh;
    literal.replace(QLatin1Char('\''), QStringLiteral("''"));
    bool ok = false;
    {
        QSqlQuery query(QSqlDatabase::database(connection));
        ok = query.exec(QStringLiteral("VACUUM INTO '%1'").arg(literal));
        if (!ok)
            *error = query.lastError().text();
    }
    closeConnection(connection);
    if (!ok) {
        QFile::remove(fresh);
        return false;
    }
    if (isPlainFile(databasePath) && !QFile::remove(databasePath)) {
        QFile::remove(fresh);
        *error = QStringLiteral("%1 cannot be replaced").arg(QDir::toNativeSeparators(databasePath));
        return false;
    }
    if (!QFile::rename(fresh, databasePath)) {
        *error = QStringLiteral("the restored copy cannot be put in place");
        return false;
    }
    return true;
}

} // namespace app
