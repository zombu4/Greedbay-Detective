// Database backups outside the work folder (core/Backups; owner decision
// 2026-10-22): the daily copy, the copies before a change, how many are
// kept, and restore. Everything in a temporary folder.
#include "core/Backups.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using app::Backups;

namespace {

bool exec(const QString &connection, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connection));
    return query.exec(sql);
}

int count(const QString &connection)
{
    QSqlQuery query(QSqlDatabase::database(connection));
    return query.exec(QStringLiteral("SELECT count(*) FROM t")) && query.next() ? query.value(0).toInt() : -1;
}

} // namespace

class TestBackups : public QObject {
    Q_OBJECT

private slots:
    void folderIsBesideTheWorkFolder();
    void dailyCopyOnceADay();
    void beforeChangeAndRestore();
};

void TestBackups::folderIsBesideTheWorkFolder()
{
    const QString folder = Backups::folderFor(QStringLiteral("Example"));
    QVERIFY(folder.endsWith(QStringLiteral("/Example-Backups")));
    QVERIFY(Backups::folderFor(QString()).isEmpty());
}

void TestBackups::dailyCopyOnceADay()
{
    QTemporaryDir temp;
    const QString dbPath = temp.filePath(QStringLiteral("app.sqlite"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("daily"));
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
    }
    QVERIFY(exec(QStringLiteral("daily"), QStringLiteral("CREATE TABLE t (x)")));
    QVERIFY(exec(QStringLiteral("daily"), QStringLiteral("INSERT INTO t VALUES (1)")));
    Backups backups(temp.filePath(QStringLiteral("backups")));
    // Older daily copies than the ones kept, made up for the test.
    QVERIFY(QDir().mkpath(backups.folder()));
    for (int day = 1; day <= 9; ++day)
        QVERIFY(writeTestFile(backups.folder() + QStringLiteral("/daily-2020-01-0%1.sqlite").arg(day), "old"));
    QString error;
    QVERIFY2(backups.daily(QStringLiteral("daily"), &error), qPrintable(error));
    QVERIFY2(backups.daily(QStringLiteral("daily"), &error), qPrintable(error)); // once a day
    const QList<Backups::Entry> list = backups.list();
    QCOMPARE(list.size(), Backups::kKeepDaily);
    const QString today = QStringLiteral("daily-%1.sqlite").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    QVERIFY(QFile::exists(backups.folder() + QLatin1Char('/') + today));
    QVERIFY(!QFile::exists(backups.folder() + QStringLiteral("/daily-2020-01-01.sqlite")));
    QSqlDatabase::database(QStringLiteral("daily")).close();
    QSqlDatabase::removeDatabase(QStringLiteral("daily"));
}

void TestBackups::beforeChangeAndRestore()
{
    QTemporaryDir temp;
    const QString dbPath = temp.filePath(QStringLiteral("app.sqlite"));
    const QString connection = QStringLiteral("restore");
    auto openDb = [&]() {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(dbPath);
        return db.open();
    };
    auto closeDb = [&]() {
        QSqlDatabase::database(connection).close();
        QSqlDatabase::removeDatabase(connection);
    };
    QVERIFY(openDb());
    QVERIFY(exec(connection, QStringLiteral("CREATE TABLE t (x)")));
    QVERIFY(exec(connection, QStringLiteral("INSERT INTO t VALUES (1)")));
    QVERIFY(exec(connection, QStringLiteral("PRAGMA user_version = 3")));
    closeDb();

    Backups backups(temp.filePath(QStringLiteral("backups")));
    QString error;
    // No upgrade pending: no copy; one pending: a copy.
    QVERIFY2(backups.beforeMigration(dbPath, 3, &error), qPrintable(error));
    QCOMPARE(backups.list().size(), 0);
    QVERIFY2(backups.beforeMigration(dbPath, 4, &error), qPrintable(error));
    QCOMPARE(backups.list().size(), 1);
    QVERIFY(backups.list().first().kind.startsWith(QStringLiteral("before migration")));
    const QString saved = backups.list().first().name;
    // No database yet: nothing to keep, and fine.
    QVERIFY(backups.beforeChange(temp.filePath(QStringLiteral("none.sqlite")), QStringLiteral("uninstall"), &error));

    // A change after the copy, then the restore: the copy's content is back,
    // and the state before the restore was copied too.
    QVERIFY(openDb());
    QVERIFY(exec(connection, QStringLiteral("INSERT INTO t VALUES (2)")));
    QCOMPARE(count(connection), 2);
    closeDb();
    QVERIFY(!backups.scheduleRestore(QStringLiteral("daily-1999-01-01.sqlite"), &error));
    QVERIFY(!backups.scheduleRestore(QStringLiteral("../app.sqlite"), &error));
    QVERIFY2(backups.scheduleRestore(saved, &error), qPrintable(error));
    QCOMPARE(backups.pendingRestore(), saved);
    QVERIFY2(backups.applyPendingRestore(dbPath, &error), qPrintable(error));
    QVERIFY(backups.pendingRestore().isEmpty());
    QVERIFY(openDb());
    QCOMPARE(count(connection), 1);
    closeDb();
    bool beforeRestore = false;
    for (const Backups::Entry &entry : backups.list())
        beforeRestore = beforeRestore || entry.kind == QLatin1String("before restore");
    QVERIFY(beforeRestore);
    // Nothing pending: nothing happens.
    QVERIFY(backups.applyPendingRestore(dbPath, &error));
}

QTEST_GUILESS_MAIN(TestBackups)
#include "tst_backups.moc"
