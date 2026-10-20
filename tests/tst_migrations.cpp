#include "db/Migrations.h"
#include "support/TestFiles.h"

#include <QTemporaryDir>

#include <QTest>

using app::Migration;

class TestMigrations : public QObject {
    Q_OBJECT

private slots:
    void shippedMigrationsLoad();
    void contiguityCheck();
    void loadingRefusesBadFolders();
};

void TestMigrations::shippedMigrationsLoad()
{
    QString error;
    const auto migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
    QVERIFY2(migrations, qPrintable(error));
    QCOMPARE(migrations->size(), 5);
    const Migration &init = migrations->first();
    QCOMPARE(init.version, 1);
    QCOMPARE(init.name, QStringLiteral("init"));
    // Five tables and two indexes.
    QCOMPARE(init.statements.size(), 7);
    QVERIFY(init.statements.first().startsWith(QStringLiteral("CREATE TABLE lookup")));
    const Migration &saved = migrations->at(1);
    QCOMPARE(saved.version, 2);
    QCOMPARE(saved.name, QStringLiteral("saved_lookups"));
    // Four tables, three indexes, two copies of version-1 rows, five drops.
    QCOMPARE(saved.statements.size(), 14);
    QVERIFY(saved.statements.first().startsWith(QStringLiteral("CREATE TABLE lookups")));
    QVERIFY(saved.statements.last().startsWith(QStringLiteral("DROP TABLE lookup")));
    const Migration &formats = migrations->at(2);
    QCOMPARE(formats.version, 3);
    QCOMPARE(formats.name, QStringLiteral("formats"));
    // Eight columns of lookups, five of lookup_rows.
    QCOMPARE(formats.statements.size(), 13);
    const Migration &ratings = migrations->at(3);
    QCOMPARE(ratings.version, 4);
    QCOMPARE(ratings.name, QStringLiteral("seller_ratings"));
    // Two columns of lookups.
    QCOMPARE(ratings.statements.size(), 2);
    const Migration &sellers = migrations->at(4);
    QCOMPARE(sellers.version, 5);
    QCOMPARE(sellers.name, QStringLiteral("sellers"));
    // One column of lookups, two of lookup_rows, two tables.
    QCOMPARE(sellers.statements.size(), 5);
}

void TestMigrations::contiguityCheck()
{
    QVERIFY(app::migrationsAreContiguous({}));
    QVERIFY(app::migrationsAreContiguous({Migration{1, QStringLiteral("a"), {}}, Migration{2, QStringLiteral("b"), {}}}));
    QVERIFY(!app::migrationsAreContiguous({Migration{1, QStringLiteral("a"), {}}, Migration{3, QStringLiteral("c"), {}}}));
    QVERIFY(!app::migrationsAreContiguous({Migration{2, QStringLiteral("b"), {}}}));
    QString error;
    QVERIFY(!app::loadMigrations(QStringLiteral(":/no-such-folder"), &error));
}

void TestMigrations::loadingRefusesBadFolders()
{
    QString error;
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0001_first.sql"), "CREATE TABLE a (x);"));
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/notes.txt"), "synthetic"));
        QVERIFY(!app::loadMigrations(tmp.path(), &error));
        QVERIFY2(error.contains(QStringLiteral("unexpected file")), qPrintable(error));
    }
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0001_first.sql"), "-- only a comment\n;\n"));
        QVERIFY(!app::loadMigrations(tmp.path(), &error));
        QVERIFY2(error.contains(QStringLiteral("no statements")), qPrintable(error));
    }
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0001_first.sql"), "CREATE TABLE a (x);"));
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0003_third.sql"), "CREATE TABLE c (x);"));
        QVERIFY(!app::loadMigrations(tmp.path(), &error));
        QVERIFY2(error.contains(QStringLiteral("without gaps")), qPrintable(error));
    }
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0001_first.sql"),
                              "CREATE TRIGGER t AFTER INSERT ON a BEGIN SELECT 1; END;"));
        QVERIFY(!app::loadMigrations(tmp.path(), &error));
        QVERIFY2(error.contains(QStringLiteral("TRIGGER")), qPrintable(error));
    }
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0001_first.sql"), "CREATE TABLE a (x);"));
        QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/0002_second.sql"), "CREATE TABLE b (y);"));
        const auto migrations = app::loadMigrations(tmp.path(), &error);
        QVERIFY2(migrations, qPrintable(error));
        QCOMPARE(migrations->size(), 2);
        QCOMPARE(migrations->at(1).name, QStringLiteral("second"));
    }
}

QTEST_GUILESS_MAIN(TestMigrations)
#include "tst_migrations.moc"
