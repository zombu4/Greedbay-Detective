#include "db/SqlScript.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

using app::splitSqlScript;

class TestSqlScript : public QObject {
    Q_OBJECT

private slots:
    void splitsStatements();
    void ignoresSemicolonsInQuotesAndComments();
    void keepsEscapedQuotes();
    void dropsEmptyStatements();
    void refusesTriggers();
    void refusesUnterminatedText();
    void qsqliteRunsOnlyTheFirstStatement();
};

void TestSqlScript::splitsStatements()
{
    QString error;
    const auto statements = splitSqlScript(QStringLiteral("CREATE TABLE a (x);\nCREATE TABLE b (y);\n"), &error);
    QVERIFY2(statements, qPrintable(error));
    QCOMPARE(*statements, QStringList({QStringLiteral("CREATE TABLE a (x)"), QStringLiteral("CREATE TABLE b (y)")}));
}

void TestSqlScript::ignoresSemicolonsInQuotesAndComments()
{
    QString error;
    const auto statements = splitSqlScript(
        QStringLiteral("INSERT INTO t VALUES ('a;b'); -- c;d\n/* e;f */ SELECT \"x;y\", `p;q`, [r;s];"), &error);
    QVERIFY2(statements, qPrintable(error));
    QCOMPARE(*statements, QStringList({QStringLiteral("INSERT INTO t VALUES ('a;b')"),
                                       QStringLiteral("SELECT \"x;y\", `p;q`, [r;s]")}));
}

void TestSqlScript::keepsEscapedQuotes()
{
    QString error;
    const auto statements = splitSqlScript(QStringLiteral("SELECT 'it''s; fine';"), &error);
    QVERIFY2(statements, qPrintable(error));
    QCOMPARE(*statements, QStringList({QStringLiteral("SELECT 'it''s; fine'")}));
}

void TestSqlScript::dropsEmptyStatements()
{
    QString error;
    const auto statements = splitSqlScript(QStringLiteral(" ;;\n-- only a comment\n; SELECT 1 "), &error);
    QVERIFY2(statements, qPrintable(error));
    QCOMPARE(*statements, QStringList({QStringLiteral("SELECT 1")}));
}

void TestSqlScript::refusesTriggers()
{
    QString error;
    QVERIFY(!splitSqlScript(
        QStringLiteral("CREATE TRIGGER t AFTER INSERT ON a BEGIN SELECT 1; END;"), &error));
    QVERIFY2(error.contains(QStringLiteral("TRIGGER")), qPrintable(error));
    QVERIFY(!splitSqlScript(QStringLiteral("create temp trigger t after insert on a begin select 1; end;"), &error));
}

void TestSqlScript::refusesUnterminatedText()
{
    QString error;
    QVERIFY(!splitSqlScript(QStringLiteral("SELECT 'abc"), &error));
    QVERIFY(!splitSqlScript(QStringLiteral("SELECT 1; /* open"), &error));
    QVERIFY(!splitSqlScript(QStringLiteral("SELECT [abc"), &error));
}

// Documents why scripts are split: given two statements in one exec(), the
// QSQLITE driver does not create the second table.
void TestSqlScript::qsqliteRunsOnlyTheFirstStatement()
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("multi"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QSqlQuery query(db);
        query.exec(QStringLiteral("CREATE TABLE first_table (x); CREATE TABLE second_table (x);"));
        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'second_table'")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 0);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("multi"));
}

QTEST_GUILESS_MAIN(TestSqlScript)
#include "tst_sqlscript.moc"
