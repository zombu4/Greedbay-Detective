// The daily page cap's counter (core/PageBudget): counting, the cap, the day
// change, a clock set back, and files it must not trust or overwrite.
#include "core/PageBudget.h"
#include "support/TestFiles.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using app::PageBudget;

namespace {

const QDate kToday(2026, 10, 19);

PageBudget::Clock fixed(const QDate &date)
{
    return [date]() { return date; };
}

QJsonObject stored(const QString &path)
{
    return QJsonDocument::fromJson(readTestFile(path)).object();
}

} // namespace

class TestPageBudget : public QObject {
    Q_OBJECT

private slots:
    void countsUpToTheCap();
    void newDayStartsAtZero();
    void clockSetBackKeepsCount();
    void damagedFileIsRefusedAndKept_data();
    void damagedFileIsRefusedAndKept();
    void linkedFileIsRefused();
    void unsavableCountRefusesThePage();
};

void TestPageBudget::countsUpToTheCap()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/page-count.json");
    PageBudget budget(path, 2, fixed(kToday));
    QCOMPARE(budget.usedToday(), 0);
    QVERIFY(!QFile::exists(path));
    QString error;
    QCOMPARE(budget.take(&error), PageBudget::Take::Taken);
    QCOMPARE(budget.take(&error), PageBudget::Take::Taken);
    QCOMPARE(budget.usedToday(), 2);
    QCOMPARE(stored(path), QJsonObject({{"date", "2026-10-19"}, {"pages", 2}}));
    QCOMPARE(budget.take(&error), PageBudget::Take::CapReached);
    QVERIFY2(error.contains(QStringLiteral("loaded 2 eBay pages today (limit 2")), qPrintable(error));
    QCOMPARE(stored(path).value("pages").toInt(), 2);

    // The count holds across instances (an app restart).
    PageBudget again(path, 3, fixed(kToday));
    QCOMPARE(again.usedToday(), 2);
    QCOMPARE(again.take(&error), PageBudget::Take::Taken);
    QCOMPARE(again.take(&error), PageBudget::Take::CapReached);
}

void TestPageBudget::newDayStartsAtZero()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/page-count.json");
    QVERIFY(writeTestFile(path, R"({"date":"2026-10-18","pages":300})"));
    PageBudget budget(path, 300, fixed(kToday));
    QCOMPARE(budget.usedToday(), 0);
    QString error;
    QCOMPARE(budget.take(&error), PageBudget::Take::Taken);
    QCOMPARE(stored(path), QJsonObject({{"date", "2026-10-19"}, {"pages", 1}}));
}

void TestPageBudget::clockSetBackKeepsCount()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/page-count.json");
    QVERIFY(writeTestFile(path, R"({"date":"2026-10-20","pages":4})"));
    PageBudget budget(path, 5, fixed(kToday));
    QCOMPARE(budget.usedToday(), 4);
    QString error;
    QCOMPARE(budget.take(&error), PageBudget::Take::Taken);
    // The later date is kept, so the next real day goes on from this count.
    QCOMPARE(stored(path), QJsonObject({{"date", "2026-10-20"}, {"pages", 5}}));
    QCOMPARE(budget.take(&error), PageBudget::Take::CapReached);
}

void TestPageBudget::damagedFileIsRefusedAndKept_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::newRow("not json") << QByteArray("pages=3");
    QTest::newRow("array") << QByteArray("[1,2]");
    QTest::newRow("no date") << QByteArray(R"({"pages":3})");
    QTest::newRow("bad date") << QByteArray(R"({"date":"2026-13-40","pages":3})");
    QTest::newRow("pages text") << QByteArray(R"({"date":"2026-10-19","pages":"3"})");
    QTest::newRow("negative") << QByteArray(R"({"date":"2026-10-19","pages":-1})");
    QTest::newRow("fraction") << QByteArray(R"({"date":"2026-10-19","pages":2.5})");
    QTest::newRow("too large") << QByteArray(5000, ' ');
}

void TestPageBudget::damagedFileIsRefusedAndKept()
{
    QFETCH(QByteArray, content);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/page-count.json");
    QVERIFY(writeTestFile(path, content));
    PageBudget budget(path, 300, fixed(kToday));
    QString error;
    QCOMPARE(budget.usedToday(&error), -1);
    QVERIFY(!error.isEmpty());
    error.clear();
    QCOMPARE(budget.take(&error), PageBudget::Take::FileError);
    QVERIFY2(error.contains(QStringLiteral("delete it to start counting again")), qPrintable(error));
    QCOMPARE(readTestFile(path), content);
}

void TestPageBudget::linkedFileIsRefused()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString outside = tmp.path() + QStringLiteral("/outside.json");
    const QByteArray content = R"({"date":"2026-10-19","pages":1})";
    QVERIFY(writeTestFile(outside, content));
    const QString path = tmp.path() + QStringLiteral("/page-count.json");
    QVERIFY(QFile::link(outside, path));
    PageBudget budget(path, 300, fixed(kToday));
    QString error;
    QCOMPARE(budget.take(&error), PageBudget::Take::FileError);
    QVERIFY2(error.contains(QStringLiteral("is a link")), qPrintable(error));
    QCOMPARE(readTestFile(outside), content);
}

void TestPageBudget::unsavableCountRefusesThePage()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // The folder for the file does not exist, so nothing can be saved.
    const QString path = tmp.path() + QStringLiteral("/missing/page-count.json");
    PageBudget budget(path, 300, fixed(kToday));
    QString error;
    QCOMPARE(budget.take(&error), PageBudget::Take::FileError);
    QVERIFY2(error.contains(QStringLiteral("cannot be saved")), qPrintable(error));
    QVERIFY(!QFile::exists(path));
}

QTEST_GUILESS_MAIN(TestPageBudget)
#include "tst_pagebudget.moc"
