#include "db/Database.h"
#include "db/Migrations.h"
#include "support/TestFiles.h"
#include "support/TestMigrations.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTest>
#include <QTime>
#include <QTimeZone>

using app::Database;
using app::Migration;

namespace {

// Runs statements on a separate, plain connection (no migrations). With
// `keepOpen` the connection stays open under the name "held" until closeHeld().
bool execRaw(const QString &path, const QStringList &statements, QString *error, bool keepOpen = false)
{
    const QString name = keepOpen ? QStringLiteral("held") : QStringLiteral("raw");
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) {
            *error = db.lastError().text();
            ok = false;
        }
        for (const QString &statement : statements) {
            if (!ok)
                break;
            QSqlQuery query(db);
            if (!query.exec(statement)) {
                *error = statement + QStringLiteral(": ") + query.lastError().text();
                ok = false;
            }
        }
        if (!keepOpen)
            db.close();
    }
    if (!keepOpen)
        QSqlDatabase::removeDatabase(name);
    return ok;
}

void closeHeld()
{
    {
        QSqlDatabase db = QSqlDatabase::database(QStringLiteral("held"), false);
        if (db.isValid())
            db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("held"));
}

// First column of the first row, on a separate plain connection.
QVariant queryRaw(const QString &path, const QString &sql)
{
    const QString name = QStringLiteral("rawquery");
    QVariant value;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(sql) && query.next())
                value = query.value(0);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return value;
}

// Everything a database holds, as text: user_version, every schema object
// with its SQL, and every row of every table in rowid order. A database
// restored from a VACUUM INTO backup is not byte-identical to the original
// (VACUUM rewrites the file), so restores are compared with this instead.
QString dumpDatabase(const QString &path)
{
    const QString name = QStringLiteral("dump");
    QString dump;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery version(db);
            if (version.exec(QStringLiteral("PRAGMA user_version")) && version.next())
                dump += QStringLiteral("user_version=%1\n").arg(version.value(0).toInt());
            QStringList tables;
            QSqlQuery schema(db);
            if (schema.exec(QStringLiteral("SELECT type, name, sql FROM sqlite_master ORDER BY type, name"))) {
                while (schema.next()) {
                    dump += schema.value(0).toString() + QLatin1Char(' ') + schema.value(1).toString()
                            + QLatin1Char(' ') + schema.value(2).toString() + QLatin1Char('\n');
                    if (schema.value(0).toString() == QLatin1String("table"))
                        tables.append(schema.value(1).toString());
                }
            }
            for (const QString &table : tables) {
                QSqlQuery rows(db);
                if (!rows.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY rowid").arg(table)))
                    continue;
                while (rows.next()) {
                    dump += table + QLatin1Char(':');
                    for (int column = 0; column < rows.record().count(); ++column)
                        dump += QLatin1Char('|') + rows.value(column).toString();
                    dump += QLatin1Char('\n');
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return dump;
}

QVariant queryOn(const QString &connection, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connection));
    if (query.exec(sql) && query.next())
        return query.value(0);
    return {};
}

QStringList backupFiles(const QString &dir)
{
    return QDir(dir).entryList({QStringLiteral(TEST_APP_NAME "-v*.sqlite")}, QDir::Files, QDir::Name);
}

QList<Migration> shippedMigrations()
{
    QString error;
    const auto migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
    return migrations ? *migrations : QList<Migration>{};
}

// Migrations 1 and 2 of parentChildMigrations, then a migration 3 that fails.
QList<Migration> twoGoodThenBroken()
{
    QList<Migration> migrations = parentChildMigrations(2);
    migrations.append(Migration{3, QStringLiteral("broken"),
                                {QStringLiteral("CREATE TABLE half_done (x)"), QStringLiteral("THIS IS NOT SQL")}});
    return migrations;
}

bool makeBackupFile(const QString &dir, const QDateTime &when, const QByteArray &content = "synthetic backup")
{
    return QDir().mkpath(dir)
           && writeTestFile(QDir(dir).filePath(Database::backupFileName(QStringLiteral(TEST_APP_NAME), 1, when)),
                            content);
}

} // namespace

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createsTheShippedSchema();
    void reopeningChangesNothing();
    void upgradeKeepsChildRowsAndMakesABackup();
    void failedMigrationRollsBackToTheOriginalData();
    void foreignKeyViolationRollsBackToTheOriginalData();
    void restoreUndoesAnAlreadyCommittedMigration();
    void failedRestoreLeavesAConsistentDatabase();
    void failureOnANewDatabaseLeavesNothing();
    void existingEmptyFileIsUpgradedWithABackup();
    void dataOnlyInTheWriteAheadLogIsBackedUpAndMigrated();
    void failedBackupLeavesNoPartialFile();
    void newerSchemaIsRefusedAndUntouched();
    void foreignDatabaseIsRefusedAndUntouched();
    void missingDatabaseWithBackupsIsRefused();
    void lowDiskSpaceRefusesTheUpgrade();
    void crashBetweenMigrationsLeavesAConsistentVersion();
    void upgradeKeepsOnlyTheNewestBackups();
    void failedUpgradeAlsoBoundsTheBackups();
    void newBackupSurvivesAClockThatWentBack();
    void rotationTouchesOnlyMatchingRegularFiles();
    void rotationKeepCountIsAtLeastOne();
    void rotationRefusesALinkedBackupsFolder();
    void backupFileNameFormat();
    void userVersionIsTransactional();
    void refusesLinksBusyConnectionsAndGaps();

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_db;
    QString m_backups;
};

void TestDatabase::init()
{
    m_tmp = new QTemporaryDir;
    QVERIFY(m_tmp->isValid());
    m_db = m_tmp->path() + QStringLiteral("/" TEST_APP_NAME ".sqlite");
    m_backups = m_tmp->path() + QStringLiteral("/backups");
}

void TestDatabase::cleanup()
{
    for (const QString &name : QSqlDatabase::connectionNames())
        Database::close(name);
    QFile::setPermissions(m_backups, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    delete m_tmp;
    m_tmp = nullptr;
}

void TestDatabase::createsTheShippedSchema()
{
    const QList<Migration> migrations = shippedMigrations();
    QVERIFY(!migrations.isEmpty());
    const Database::Result result = Database::open(QStringLiteral("main"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), migrations);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.outcome, Database::Outcome::Created);
    QCOMPARE(result.fromVersion, 0);
    QCOMPARE(result.toVersion, 1);
    QVERIFY(result.backupPath.isEmpty());

    QCOMPARE(queryOn(QStringLiteral("main"), QStringLiteral("PRAGMA user_version")).toInt(), 1);
    QCOMPARE(queryOn(QStringLiteral("main"), QStringLiteral("PRAGMA application_id")).toInt(),
             Database::kApplicationId);
    QCOMPARE(queryOn(QStringLiteral("main"), QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    QCOMPARE(queryOn(QStringLiteral("main"), QStringLiteral("PRAGMA temp_store")).toInt(), 2);
    for (const char *table : {"lookup", "listing", "research_summary", "valuation", "excluded_listing"}) {
        const QVariant count = queryOn(
            QStringLiteral("main"),
            QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = '%1'")
                .arg(QLatin1String(table)));
        QVERIFY2(count.toInt() == 1, table);
    }

    // The schema's constraints hold: a listing needs an existing lookup.
    QSqlQuery insert(QSqlDatabase::database(QStringLiteral("main")));
    QVERIFY(!insert.exec(QStringLiteral(
        "INSERT INTO listing (lookup_id, role, source, price_status) VALUES (42, 'sold', 'sold_search', 'unconfirmed')")));
}

void TestDatabase::reopeningChangesNothing()
{
    const QList<Migration> migrations = shippedMigrations();
    QVERIFY(Database::open(QStringLiteral("a"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations).ok());
    Database::close(QStringLiteral("a"));
    const QByteArray before = readTestFile(m_db);

    const Database::Result result =
        Database::open(QStringLiteral("b"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.outcome, Database::Outcome::Opened);
    QCOMPARE(queryOn(QStringLiteral("b"), QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    QCOMPARE(queryOn(QStringLiteral("b"), QStringLiteral("PRAGMA temp_store")).toInt(), 2);
    Database::close(QStringLiteral("b"));
    QCOMPARE(readTestFile(m_db), before);
    QVERIFY(backupFiles(m_backups).isEmpty());
}

void TestDatabase::upgradeKeepsChildRowsAndMakesABackup()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha'), (2, 'beta')"),
                      QStringLiteral("INSERT INTO child (id, parent_id) VALUES (10, 1), (11, 1), (12, 2)")},
                     &error),
             qPrintable(error));

    const Database::Result result = Database::open(QStringLiteral("v2"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.outcome, Database::Outcome::Upgraded);
    QCOMPARE(result.fromVersion, 1);
    QCOMPARE(result.toVersion, 2);

    // Dropping the old parent table did not cascade into child.
    QCOMPARE(queryOn(QStringLiteral("v2"), QStringLiteral("SELECT count(*) FROM child")).toInt(), 3);
    QCOMPARE(queryOn(QStringLiteral("v2"), QStringLiteral("SELECT count(*) FROM parent")).toInt(), 2);
    QCOMPARE(queryOn(QStringLiteral("v2"),
                     QStringLiteral("SELECT count(*) FROM pragma_table_info('parent') WHERE name = 'note'"))
                 .toInt(),
             1);
    QCOMPARE(queryOn(QStringLiteral("v2"), QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    Database::close(QStringLiteral("v2"));

    // The backup holds the version-1 database with its rows.
    QVERIFY(QFileInfo(result.backupPath).isFile());
    QCOMPARE(QFileInfo(result.backupPath).dir().absolutePath(), QDir(m_backups).absolutePath());
    QVERIFY(QRegularExpression(QStringLiteral("^" TEST_APP_NAME "-v1-\\d{8}T\\d{9}Z\\.sqlite$"))
                .match(QFileInfo(result.backupPath).fileName())
                .hasMatch());
    QCOMPARE(queryRaw(result.backupPath, QStringLiteral("PRAGMA user_version")).toInt(), 1);
    QCOMPARE(queryRaw(result.backupPath, QStringLiteral("SELECT count(*) FROM child")).toInt(), 3);
}

void TestDatabase::failedMigrationRollsBackToTheOriginalData()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db, {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha')")}, &error),
             qPrintable(error));
    const QString before = dumpDatabase(m_db);
    QVERIFY(before.contains(QStringLiteral("parent:|1|alpha")));

    QList<Migration> migrations = parentChildMigrations(1);
    migrations.append(Migration{2, QStringLiteral("broken"),
                                {QStringLiteral("CREATE TABLE half_done (x)"), QStringLiteral("THIS IS NOT SQL")}});
    const Database::Result result =
        Database::open(QStringLiteral("bad"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY2(result.error.contains(QStringLiteral("migration 2")), qPrintable(result.error));
    QVERIFY(!QSqlDatabase::contains(QStringLiteral("bad")));
    QCOMPARE(dumpDatabase(m_db), before);
    QVERIFY(!QFileInfo::exists(m_db + QStringLiteral("-journal")));
    QCOMPARE(backupFiles(m_backups).size(), 1); // the backup is kept
}

void TestDatabase::foreignKeyViolationRollsBackToTheOriginalData()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db, {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha')")}, &error),
             qPrintable(error));
    const QString before = dumpDatabase(m_db);

    QList<Migration> migrations = parentChildMigrations(1);
    migrations.append(Migration{2, QStringLiteral("orphan"),
                                {QStringLiteral("INSERT INTO child (id, parent_id) VALUES (99, 12345)")}});
    const Database::Result result =
        Database::open(QStringLiteral("fk"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY2(result.error.contains(QStringLiteral("foreign key")), qPrintable(result.error));
    QCOMPARE(dumpDatabase(m_db), before);
}

// Migration 2 commits, migration 3 fails: ROLLBACK alone would leave version
// 2, so only the restore from the backup brings back version 1.
void TestDatabase::restoreUndoesAnAlreadyCommittedMigration()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha')"),
                      QStringLiteral("INSERT INTO child (id, parent_id) VALUES (10, 1)")},
                     &error),
             qPrintable(error));
    const QString before = dumpDatabase(m_db);
    QVERIFY(before.startsWith(QStringLiteral("user_version=1\n")));

    QList<int> committed;
    Database::Options options;
    options.afterMigration = [&committed](int version) { committed.append(version); };
    const Database::Result result = Database::open(QStringLiteral("restore"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), twoGoodThenBroken(), options);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QCOMPARE(committed, QList<int>({2}));
    QVERIFY2(!result.error.contains(QStringLiteral("RESTORE FAILED")), qPrintable(result.error));
    QCOMPARE(dumpDatabase(m_db), before);
}

void TestDatabase::failedRestoreLeavesAConsistentDatabase()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha')"),
                      QStringLiteral("INSERT INTO child (id, parent_id) VALUES (10, 1)")},
                     &error),
             qPrintable(error));

    // The backup disappears after migration 2 commits, so the restore fails.
    const QString backups = m_backups;
    Database::Options options;
    options.afterMigration = [backups](int version) {
        if (version == 2) {
            for (const QString &name : backupFiles(backups))
                QFile::remove(QDir(backups).filePath(name));
        }
    };
    const Database::Result result = Database::open(QStringLiteral("norestore"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), twoGoodThenBroken(), options);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY2(result.error.contains(QStringLiteral("RESTORE FAILED")), qPrintable(result.error));

    // The database is still there, at the last committed version, with its rows.
    QVERIFY(QFileInfo(m_db).isFile());
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA user_version")).toInt(), 2);
    QCOMPARE(queryRaw(m_db, QStringLiteral("SELECT count(*) FROM child")).toInt(), 1);
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
}

void TestDatabase::failureOnANewDatabaseLeavesNothing()
{
    const QList<Migration> migrations = {
        Migration{1, QStringLiteral("broken"), {QStringLiteral("CREATE TABLE t (x)"), QStringLiteral("NOT SQL")}}};
    const Database::Result result =
        Database::open(QStringLiteral("new"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY(!QFileInfo::exists(m_db));
    QVERIFY(!QFileInfo::exists(m_db + QStringLiteral("-journal")));
    QVERIFY(backupFiles(m_backups).isEmpty());
}

void TestDatabase::existingEmptyFileIsUpgradedWithABackup()
{
    QVERIFY(writeTestFile(m_db, QByteArray()));
    const Database::Result result = Database::open(QStringLiteral("empty"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(1));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.outcome, Database::Outcome::Upgraded);
    QCOMPARE(result.fromVersion, 0);
    QVERIFY(QFileInfo(result.backupPath).isFile());
    Database::close(QStringLiteral("empty"));

    // A failure on an existing empty file restores it instead of deleting it.
    QTemporaryDir other;
    QVERIFY(other.isValid());
    const QString db = other.path() + QStringLiteral("/" TEST_APP_NAME ".sqlite");
    QVERIFY(writeTestFile(db, QByteArray()));
    const QList<Migration> broken = {
        Migration{1, QStringLiteral("broken"), {QStringLiteral("CREATE TABLE t (x)"), QStringLiteral("NOT SQL")}}};
    const Database::Result failed = Database::open(QStringLiteral("emptyfail"), db,
                                                   other.path() + QStringLiteral("/backups"),
                                                   QStringLiteral(TEST_APP_NAME), broken);
    QCOMPARE(failed.outcome, Database::Outcome::Failed);
    QVERIFY(QFileInfo(db).isFile());
    QCOMPARE(queryRaw(db, QStringLiteral("SELECT count(*) FROM sqlite_master")).toInt(), 0);
}

void TestDatabase::dataOnlyInTheWriteAheadLogIsBackedUpAndMigrated()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    // Another connection switches to WAL and writes; it stays open, so the
    // rows live in the -wal file until the end of the test.
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("PRAGMA journal_mode = WAL"),
                      QStringLiteral("PRAGMA wal_autocheckpoint = 0"),
                      QStringLiteral("INSERT INTO parent (id, name) VALUES (7, 'in-wal')")},
                     &error, true),
             qPrintable(error));
    QVERIFY(QFileInfo(m_db + QStringLiteral("-wal")).size() > 0);

    const Database::Result result = Database::open(QStringLiteral("wal"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(queryOn(QStringLiteral("wal"), QStringLiteral("SELECT name FROM parent WHERE id = 7")).toString(),
             QStringLiteral("in-wal"));
    Database::close(QStringLiteral("wal"));
    closeHeld();
    QCOMPARE(queryRaw(result.backupPath, QStringLiteral("SELECT name FROM parent WHERE id = 7")).toString(),
             QStringLiteral("in-wal"));
}

void TestDatabase::failedBackupLeavesNoPartialFile()
{
#ifdef Q_OS_WIN
    QSKIP("Folder permissions cannot block file creation for the owner on Windows");
#else
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QVERIFY(QDir().mkpath(m_backups));
    const QString before = dumpDatabase(m_db);
    QVERIFY(QFile::setPermissions(m_backups, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    if (permissionsIgnored(m_backups))
        QSKIP("This process ignores folder permissions (for example root)");

    const Database::Result result = Database::open(QStringLiteral("nobackup"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2));
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY(result.backupPath.isEmpty());
    QVERIFY(QFile::setPermissions(m_backups, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QVERIFY(QDir(m_backups).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
    QCOMPARE(dumpDatabase(m_db), before);
#endif
}

void TestDatabase::newerSchemaIsRefusedAndUntouched()
{
    QString error;
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("CREATE TABLE future (x)"),
                      QStringLiteral("PRAGMA application_id = %1").arg(Database::kApplicationId),
                      QStringLiteral("PRAGMA user_version = 99")},
                     &error),
             qPrintable(error));
    const QByteArray before = readTestFile(m_db);

    const Database::Result result = Database::open(QStringLiteral("newer"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), shippedMigrations());
    QCOMPARE(result.outcome, Database::Outcome::RefusedNewer);
    QCOMPARE(result.fromVersion, 99);
    QVERIFY2(result.error.contains(QStringLiteral("newer")), qPrintable(result.error));
    QVERIFY(!QSqlDatabase::contains(QStringLiteral("newer")));
    QCOMPARE(readTestFile(m_db), before);
    QVERIFY(backupFiles(m_backups).isEmpty());
}

void TestDatabase::foreignDatabaseIsRefusedAndUntouched()
{
    QString error;
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("CREATE TABLE someone_elses (x)"),
                      QStringLiteral("INSERT INTO someone_elses VALUES ('synthetic')")},
                     &error),
             qPrintable(error));
    const QByteArray before = readTestFile(m_db);

    const Database::Result result = Database::open(QStringLiteral("foreign"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), shippedMigrations());
    QCOMPARE(result.outcome, Database::Outcome::NotOurs);
    QVERIFY2(result.error.contains(QStringLiteral("not created by this app")), qPrintable(result.error));
    QCOMPARE(readTestFile(m_db), before);
    QVERIFY(backupFiles(m_backups).isEmpty());
}

void TestDatabase::missingDatabaseWithBackupsIsRefused()
{
    QVERIFY(makeBackupFile(m_backups, QDateTime(QDate(2026, 1, 1), QTime(9, 0), QTimeZone::UTC)));
    const Database::Result result = Database::open(QStringLiteral("missing"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), shippedMigrations());
    QCOMPARE(result.outcome, Database::Outcome::MissingWithBackups);
    QVERIFY(!QFileInfo::exists(m_db));
    QCOMPARE(backupFiles(m_backups).size(), 1);
}

void TestDatabase::lowDiskSpaceRefusesTheUpgrade()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    QString error;
    QVERIFY2(execRaw(m_db, {QStringLiteral("INSERT INTO parent (id, name) VALUES (1, 'alpha')")}, &error),
             qPrintable(error));
    const QByteArray before = readTestFile(m_db);

    Database::Options options;
    options.freeBytes = [](const QString &) -> qint64 { return 0; };
    const Database::Result result = Database::open(QStringLiteral("full"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2), options);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    QVERIFY2(result.error.contains(QStringLiteral("free disk space")), qPrintable(result.error));
    QCOMPARE(readTestFile(m_db), before);
    QVERIFY(backupFiles(m_backups).isEmpty());
}

void TestDatabase::crashBetweenMigrationsLeavesAConsistentVersion()
{
    QProcess helper;
    helper.start(QStringLiteral(DBCRASH_PATH), {m_db, m_backups});
    QVERIFY(helper.waitForFinished(30000));
    QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
    QCOMPARE(helper.exitCode(), 3);

    // Migrations 1 and 2 committed, 3 never ran.
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA user_version")).toInt(), 2);
    QCOMPARE(queryRaw(m_db, QStringLiteral("SELECT count(*) FROM pragma_table_info('parent') WHERE name = 'note'"))
                 .toInt(),
             1);
    QCOMPARE(queryRaw(m_db, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'extra'")).toInt(), 0);
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));

    const Database::Result result = Database::open(QStringLiteral("resume"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(3));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.outcome, Database::Outcome::Upgraded);
    QCOMPARE(result.fromVersion, 2);
    QCOMPARE(result.toVersion, 3);
    QVERIFY(QFileInfo(result.backupPath).fileName().startsWith(QStringLiteral(TEST_APP_NAME "-v2-")));
}

void TestDatabase::upgradeKeepsOnlyTheNewestBackups()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    for (int day = 1; day <= 6; ++day)
        QVERIFY(makeBackupFile(m_backups, QDateTime(QDate(2020, 1, day), QTime(12, 0), QTimeZone::UTC)));

    const Database::Result result = Database::open(QStringLiteral("v2"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY2(result.warning.isEmpty(), qPrintable(result.warning));
    const QStringList left = backupFiles(m_backups);
    QCOMPARE(left.size(), 5);
    QVERIFY(left.contains(QFileInfo(result.backupPath).fileName()));
    QVERIFY(!left.contains(QStringLiteral(TEST_APP_NAME "-v1-20200101T120000000Z.sqlite")));
    QVERIFY(!left.contains(QStringLiteral(TEST_APP_NAME "-v1-20200102T120000000Z.sqlite")));
}

void TestDatabase::failedUpgradeAlsoBoundsTheBackups()
{
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    for (int day = 1; day <= 6; ++day)
        QVERIFY(makeBackupFile(m_backups, QDateTime(QDate(2020, 2, day), QTime(12, 0), QTimeZone::UTC)));

    QList<Migration> migrations = parentChildMigrations(1);
    migrations.append(Migration{2, QStringLiteral("broken"), {QStringLiteral("NOT SQL")}});
    const Database::Result result =
        Database::open(QStringLiteral("bad"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), migrations);
    QCOMPARE(result.outcome, Database::Outcome::Failed);
    const QStringList left = backupFiles(m_backups);
    QCOMPARE(left.size(), 5);
    QVERIFY(left.contains(QFileInfo(result.backupPath).fileName()));
}

void TestDatabase::newBackupSurvivesAClockThatWentBack()
{
    // Five backups dated in the future: the backup made now sorts as the oldest.
    QVERIFY(Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    Database::close(QStringLiteral("v1"));
    for (int day = 1; day <= 5; ++day)
        QVERIFY(makeBackupFile(m_backups, QDateTime(QDate(2099, 1, day), QTime(12, 0), QTimeZone::UTC)));

    const Database::Result result = Database::open(QStringLiteral("v2"), m_db, m_backups,
                                                   QStringLiteral(TEST_APP_NAME), parentChildMigrations(2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(QFileInfo(result.backupPath).isFile());
    QCOMPARE(backupFiles(m_backups).size(), 5);
}

void TestDatabase::rotationTouchesOnlyMatchingRegularFiles()
{
    QStringList names;
    for (int day = 1; day <= 7; ++day) {
        const QDateTime when(QDate(2021, 3, day), QTime(8, 30), QTimeZone::UTC);
        QVERIFY(makeBackupFile(m_backups, when));
        names.append(Database::backupFileName(QStringLiteral(TEST_APP_NAME), 1, when));
    }
    for (const char *other : {"unrelated.sqlite", TEST_APP_NAME "-v1-bad.sqlite", "OtherApp-v1-20210310T083000000Z.sqlite"})
        QVERIFY(writeTestFile(QDir(m_backups).filePath(QLatin1String(other)), "keep me"));
    QString linkName;
    const QString outside = m_tmp->path() + QStringLiteral("/outside.sqlite");
    if (kCanLinkFiles) {
        QVERIFY(writeTestFile(outside, "must survive"));
        linkName = Database::backupFileName(QStringLiteral(TEST_APP_NAME), 1,
                                            QDateTime(QDate(2021, 1, 1), QTime(0, 0), QTimeZone::UTC));
        QVERIFY(QFile::link(outside, QDir(m_backups).filePath(linkName)));
    }

    QString error;
    QVERIFY2(Database::rotateBackups(m_backups, QStringLiteral(TEST_APP_NAME), 5, QString(), &error), qPrintable(error));

    for (int i = 0; i < 7; ++i)
        QCOMPARE(QFileInfo::exists(QDir(m_backups).filePath(names.at(i))), i >= 2);
    for (const char *other : {"unrelated.sqlite", TEST_APP_NAME "-v1-bad.sqlite", "OtherApp-v1-20210310T083000000Z.sqlite"})
        QVERIFY2(QFileInfo::exists(QDir(m_backups).filePath(QLatin1String(other))), other);
    if (kCanLinkFiles) {
        QVERIFY(QFileInfo(QDir(m_backups).filePath(linkName)).isSymLink());
        QCOMPARE(readTestFile(outside), QByteArray("must survive"));
    }

    // keep = 2
    QVERIFY2(Database::rotateBackups(m_backups, QStringLiteral(TEST_APP_NAME), 2, QString(), &error), qPrintable(error));
    QCOMPARE(QFileInfo::exists(QDir(m_backups).filePath(names.at(6))), true);
    QCOMPARE(QFileInfo::exists(QDir(m_backups).filePath(names.at(5))), true);
    QCOMPARE(QFileInfo::exists(QDir(m_backups).filePath(names.at(4))), false);
}

void TestDatabase::rotationKeepCountIsAtLeastOne()
{
    for (int day = 1; day <= 3; ++day)
        QVERIFY(makeBackupFile(m_backups, QDateTime(QDate(2022, 5, day), QTime(1, 0), QTimeZone::UTC)));
    QString error;
    QVERIFY2(Database::rotateBackups(m_backups, QStringLiteral(TEST_APP_NAME), 0, QString(), &error), qPrintable(error));
    QCOMPARE(backupFiles(m_backups),
             QStringList({Database::backupFileName(QStringLiteral(TEST_APP_NAME), 1,
                                                   QDateTime(QDate(2022, 5, 3), QTime(1, 0), QTimeZone::UTC))}));
}

void TestDatabase::rotationRefusesALinkedBackupsFolder()
{
    const QString real = m_tmp->path() + QStringLiteral("/real-backups");
    for (int day = 1; day <= 7; ++day)
        QVERIFY(makeBackupFile(real, QDateTime(QDate(2023, 6, day), QTime(1, 0), QTimeZone::UTC)));
    const QString link = m_tmp->path() + QStringLiteral("/linked-backups");
    QVERIFY(makeDirectoryLink(real, link));
    QString error;
    QVERIFY(!Database::rotateBackups(link, QStringLiteral(TEST_APP_NAME), 5, QString(), &error));
    QVERIFY2(error.contains(QStringLiteral("link")), qPrintable(error));
    QCOMPARE(backupFiles(real).size(), 7);
}

void TestDatabase::backupFileNameFormat()
{
    const QDateTime when(QDate(2026, 10, 18), QTime(5, 6, 7, 89), QTimeZone::UTC);
    QCOMPARE(Database::backupFileName(QStringLiteral(TEST_APP_NAME), 3, when),
             QStringLiteral(TEST_APP_NAME "-v3-20261018T050607089Z.sqlite"));
}

// The migration runner sets user_version inside each migration's
// transaction; this checks that SQLite rolls it back with the transaction.
void TestDatabase::userVersionIsTransactional()
{
    QString error;
    QVERIFY2(execRaw(m_db, {QStringLiteral("CREATE TABLE t (x)"), QStringLiteral("PRAGMA user_version = 1")}, &error),
             qPrintable(error));
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("BEGIN IMMEDIATE"), QStringLiteral("PRAGMA user_version = 5"),
                      QStringLiteral("ROLLBACK")},
                     &error),
             qPrintable(error));
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA user_version")).toInt(), 1);
    QVERIFY2(execRaw(m_db,
                     {QStringLiteral("BEGIN IMMEDIATE"), QStringLiteral("PRAGMA user_version = 6"),
                      QStringLiteral("COMMIT")},
                     &error),
             qPrintable(error));
    QCOMPARE(queryRaw(m_db, QStringLiteral("PRAGMA user_version")).toInt(), 6);
}

void TestDatabase::refusesLinksBusyConnectionsAndGaps()
{
    QVERIFY(Database::open(QStringLiteral("busy"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
                           parentChildMigrations(1))
                .ok());
    const Database::Result again = Database::open(QStringLiteral("busy"), m_db, m_backups,
                                                  QStringLiteral(TEST_APP_NAME), parentChildMigrations(1));
    QCOMPARE(again.outcome, Database::Outcome::Failed);
    QVERIFY2(again.error.contains(QStringLiteral("already open")), qPrintable(again.error));
    Database::close(QStringLiteral("busy"));

    const Database::Result gaps = Database::open(
        QStringLiteral("gaps"), m_db, m_backups, QStringLiteral(TEST_APP_NAME),
        {Migration{2, QStringLiteral("two"), {QStringLiteral("CREATE TABLE t (x)")}}});
    QCOMPARE(gaps.outcome, Database::Outcome::Failed);

    if (kCanLinkFiles) {
        const QString link = m_tmp->path() + QStringLiteral("/linked.sqlite");
        QVERIFY(QFile::link(m_db, link));
        const Database::Result linked = Database::open(QStringLiteral("linked"), link, m_backups,
                                                       QStringLiteral(TEST_APP_NAME), parentChildMigrations(1));
        QCOMPARE(linked.outcome, Database::Outcome::Failed);
        QVERIFY2(linked.error.contains(QStringLiteral("link")), qPrintable(linked.error));
    }
}

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
