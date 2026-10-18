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
    QCOMPARE(migrations->size(), 1);
    const Migration &init = migrations->first();
    QCOMPARE(init.version, 1);
    QCOMPARE(init.name, QStringLiteral("init"));
    // Five tables and two indexes.
    QCOMPARE(init.statements.size(), 7);
    QVERIFY(init.statements.first().startsWith(QStringLiteral("CREATE TABLE lookup")));
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
