#include "core/Settings.h"
#include "support/TestFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using app::Settings;
using app::ValueSettings;

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultsWhenNoFile();
    void persistsAcrossInstances();
    void rejectsInvalidDays();
    void invalidStoredValuesReadAsDefault();
    void writesIniText();
    void leavesNoOtherFiles();
    void fileNameIsGivenPath();
    void nativeStoreIsNotUsed();
    void preferredBrowser();
    void pageDelay();
    void dailyPageCap();
    void sellerSettings();
    void searchDepths();
    void valueSettings();
    void updateCheck();
};

// Synthetic names, so the native settings store checked below belongs to this
// test alone.
void TestSettings::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("SyntheticTestOrganization"));
    QCoreApplication::setApplicationName(QStringLiteral("SyntheticSettingsTest"));
}

void TestSettings::defaultsWhenNoFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const Settings settings(tmp.path() + QStringLiteral("/settings.ini"));
    QCOMPARE(settings.soldHistoryDays(), 1095);
    QCOMPARE(settings.soldHistoryDays(), Settings::kDefaultSoldHistoryDays);
}

void TestSettings::persistsAcrossInstances()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QVERIFY(settings.setSoldHistoryDays(30));
        QVERIFY(settings.sync());
    }
    const Settings again(path);
    QCOMPARE(again.soldHistoryDays(), 30);
}

void TestSettings::rejectsInvalidDays()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Settings settings(tmp.path() + QStringLiteral("/settings.ini"));
    QVERIFY(settings.setSoldHistoryDays(30));
    QVERIFY(!settings.setSoldHistoryDays(0));
    QCOMPARE(settings.soldHistoryDays(), 30);
    QVERIFY(!settings.setSoldHistoryDays(-5));
    QCOMPARE(settings.soldHistoryDays(), 30);
    // Product Research offers at most 1095 days ("Last 3 years").
    QVERIFY(settings.setSoldHistoryDays(Settings::kMaxSoldHistoryDays));
    QCOMPARE(settings.soldHistoryDays(), 1095);
    QVERIFY(!settings.setSoldHistoryDays(1096));
    QCOMPARE(settings.soldHistoryDays(), 1095);
}

void TestSettings::invalidStoredValuesReadAsDefault()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString notNumber = tmp.path() + QStringLiteral("/notnumber.ini");
    QVERIFY(writeTestFile(notNumber, "[lookup]\nsoldHistoryDays=abc\n"));
    QCOMPARE(Settings(notNumber).soldHistoryDays(), 1095);

    const QString negative = tmp.path() + QStringLiteral("/negative.ini");
    QVERIFY(writeTestFile(negative, "[lookup]\nsoldHistoryDays=-5\n"));
    QCOMPARE(Settings(negative).soldHistoryDays(), 1095);

    const QString tooLong = tmp.path() + QStringLiteral("/toolong.ini");
    QVERIFY(writeTestFile(tooLong, "[lookup]\nsoldHistoryDays=1096\n"));
    QCOMPARE(Settings(tooLong).soldHistoryDays(), 1095);
}

void TestSettings::writesIniText()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    Settings settings(path);
    QVERIFY(settings.setSoldHistoryDays(30));
    QVERIFY(settings.sync());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    QVERIFY2(text.contains("[lookup]"), text.constData());
    QVERIFY2(text.contains("soldHistoryDays=30"), text.constData());
}

void TestSettings::leavesNoOtherFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Settings settings(tmp.path() + QStringLiteral("/settings.ini"));
    QVERIFY(settings.setSoldHistoryDays(30));
    QVERIFY(settings.sync());

    const QStringList entries = QDir(tmp.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QCOMPARE(entries, QStringList{QStringLiteral("settings.ini")});
}

void TestSettings::fileNameIsGivenPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    const Settings settings(path);
    QCOMPARE(settings.fileName(), path);
}

// Using Settings must leave the platform's native store (macOS plist,
// Windows registry, ~/.config) for this application empty.
void TestSettings::nativeStoreIsNotUsed()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    {
        Settings settings(tmp.path() + QStringLiteral("/settings.ini"));
        QVERIFY(settings.setSoldHistoryDays(45));
        QVERIFY(settings.sync());
    }
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
    // Only this application's own store, not the system-wide fallbacks that
    // QSettings also reads (on macOS the global preferences domain).
    native.setFallbacksEnabled(false);
    QCOMPARE(native.allKeys().size(), 0);
}

void TestSettings::preferredBrowser()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QCOMPARE(settings.preferredBrowser(), QString()); // automatic
        QVERIFY(!settings.setPreferredBrowser(QStringLiteral("../x")));
        QVERIFY(!settings.setPreferredBrowser(QStringLiteral("Chrome")));
        QVERIFY(settings.setPreferredBrowser(QStringLiteral("edge")));
        QVERIFY(settings.sync());
    }
    Settings again(path);
    QCOMPARE(again.preferredBrowser(), QStringLiteral("edge"));
    QVERIFY(again.setPreferredBrowser(QString()));
    QCOMPARE(again.preferredBrowser(), QString());
    // A hand-edited value that is not an id reads as automatic.
    QVERIFY(writeTestFile(path, "[browser]\npreferred=C:/x\n"));
    const Settings edited(path);
    QCOMPARE(edited.preferredBrowser(), QString());
}

void TestSettings::pageDelay()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QCOMPARE(settings.pageDelayMinMs(), 3000);
        QCOMPARE(settings.pageDelayMaxMs(), 6000);
        QVERIFY(!settings.setPageDelay(500, 2000));  // below 1 s
        QVERIFY(!settings.setPageDelay(5000, 4000)); // max below min
        QVERIFY(settings.setPageDelay(4000, 9000));
        QVERIFY(settings.sync());
    }
    const Settings again(path);
    QCOMPARE(again.pageDelayMinMs(), 4000);
    QCOMPARE(again.pageDelayMaxMs(), 9000);
    // Hand-edited files get their own paths: QSettings caches each file per
    // process and may not notice a rewrite of the same size within the same
    // millisecond.
    const QString edit = tmp.path() + QStringLiteral("/edited.ini");
    QVERIFY(writeTestFile(edit, "[pace]\npageDelayMinMs=7000\npageDelayMaxMs=2000\n"));
    const Settings edited(edit);
    QCOMPARE(edited.pageDelayMinMs(), 3000);
    QCOMPARE(edited.pageDelayMaxMs(), 6000);
}

void TestSettings::sellerSettings()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        // Owner: "flag sellers with a review rating below 96%".
        QCOMPARE(settings.sellerRatingThreshold(), 96.0);
        QCOMPARE(settings.sellerFewRatings(), 20);
        QCOMPARE(settings.sellerFeedbackCacheDays(), 7);
        QVERIFY(!settings.setSellerRatingThreshold(0));
        QVERIFY(!settings.setSellerRatingThreshold(100.5));
        QVERIFY(!settings.setSellerFewRatings(-1));
        QVERIFY(!settings.setSellerFeedbackCacheDays(-1));
        QVERIFY(!settings.setSellerFeedbackCacheDays(366));
        QVERIFY(settings.setSellerRatingThreshold(97.5));
        QVERIFY(settings.setSellerFewRatings(0));
        QVERIFY(settings.setSellerFeedbackCacheDays(0));
        QVERIFY(settings.sync());
    }
    const Settings again(path);
    QCOMPARE(again.sellerRatingThreshold(), 97.5);
    QCOMPARE(again.sellerFewRatings(), 0);
    QCOMPARE(again.sellerFeedbackCacheDays(), 0);
    const QString bad = tmp.path() + QStringLiteral("/bad.ini");
    QVERIFY(writeTestFile(bad, "[sellers]\nratingThreshold=150\nfewRatings=-3\nfeedbackCacheDays=many\n"));
    QCOMPARE(Settings(bad).sellerRatingThreshold(), 96.0);
    QCOMPARE(Settings(bad).sellerFewRatings(), 20);
    QCOMPARE(Settings(bad).sellerFeedbackCacheDays(), 7);
}

void TestSettings::dailyPageCap()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QCOMPARE(settings.dailyPageCap(), 300);
        QVERIFY(!settings.setDailyPageCap(0));
        QVERIFY(!settings.setDailyPageCap(-5));
        QVERIFY(settings.setDailyPageCap(120));
        QVERIFY(settings.sync());
    }
    QCOMPARE(Settings(path).dailyPageCap(), 120);
    const QString zero = tmp.path() + QStringLiteral("/zero.ini");
    QVERIFY(writeTestFile(zero, "[pace]\ndailyPageCap=0\n"));
    QCOMPARE(Settings(zero).dailyPageCap(), 300);
    const QString text = tmp.path() + QStringLiteral("/text.ini");
    QVERIFY(writeTestFile(text, "[pace]\ndailyPageCap=many\n"));
    QCOMPARE(Settings(text).dailyPageCap(), 300);
}

void TestSettings::searchDepths()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QCOMPARE(settings.soldDepth(), 240);
        QCOMPARE(settings.activeDepth(), 240);
        QCOMPARE(settings.researchDepth(), 200);
        QVERIFY(!settings.setSoldDepth(-1));
        QVERIFY(!settings.setActiveDepth(-1));
        QVERIFY(!settings.setResearchDepth(-1));
        // 0 skips a source.
        QVERIFY(settings.setSoldDepth(480));
        QVERIFY(settings.setActiveDepth(0));
        QVERIFY(settings.setResearchDepth(50));
        QVERIFY(settings.sync());
    }
    const Settings again(path);
    QCOMPARE(again.soldDepth(), 480);
    QCOMPARE(again.activeDepth(), 0);
    QCOMPARE(again.researchDepth(), 50);
    const QString edit = tmp.path() + QStringLiteral("/edited.ini");
    QVERIFY(writeTestFile(edit, "[lookup]\nsoldDepth=-3\nactiveDepth=x\nresearchDepth=1.5\n"));
    const Settings edited(edit);
    QCOMPARE(edited.soldDepth(), 240);
    QCOMPARE(edited.activeDepth(), 240);
    QCOMPARE(edited.researchDepth(), 200);
}

void TestSettings::updateCheck()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        // Owner answer 6: off by default.
        QCOMPARE(settings.updateCheck(), false);
        settings.setUpdateCheck(true);
        QVERIFY(settings.sync());
    }
    QCOMPARE(Settings(path).updateCheck(), true);
    {
        Settings settings(path);
        settings.setUpdateCheck(false);
        QVERIFY(settings.sync());
    }
    QCOMPARE(Settings(path).updateCheck(), false);

    const QString odd = tmp.path() + QStringLiteral("/odd.ini");
    QVERIFY(writeTestFile(odd, "[updates]\ncheck=yes\n"));
    QCOMPARE(Settings(odd).updateCheck(), false);
}

void TestSettings::valueSettings()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        const ValueSettings defaults = settings.valueSettings();
        QCOMPARE(defaults.outlierIqrFactor, 1.5);
        QCOMPARE(defaults.confidenceHigh, 10);
        QCOMPARE(defaults.confidenceMedium, 5);
        QCOMPARE(defaults.confidenceLow, 2);
        QCOMPARE(defaults.shippingFlagRatio, 2.0);
        QCOMPARE(defaults.shippingFlagMinAboveCents, qint64(1000));
        ValueSettings bad = defaults;
        bad.confidenceMedium = 20; // above high
        QVERIFY(!settings.setValueSettings(bad));
        bad = defaults;
        bad.outlierIqrFactor = 0;
        QVERIFY(!settings.setValueSettings(bad));
        ValueSettings changed = defaults;
        changed.outlierIqrFactor = 3.0;
        changed.confidenceHigh = 12;
        changed.shippingFlagMinAboveCents = 500;
        QVERIFY(settings.setValueSettings(changed));
        QVERIFY(settings.sync());
    }
    const ValueSettings again = Settings(path).valueSettings();
    QCOMPARE(again.outlierIqrFactor, 3.0);
    QCOMPARE(again.confidenceHigh, 12);
    QCOMPARE(again.shippingFlagMinAboveCents, qint64(500));
    // Hand-edited out of order: the three confidence numbers read as defaults.
    const QString edit = tmp.path() + QStringLiteral("/edited.ini");
    QVERIFY(writeTestFile(edit, "[value]\nconfidenceHigh=3\nconfidenceMedium=5\noutlierIqrFactor=-1\n"));
    const ValueSettings edited = Settings(edit).valueSettings();
    QCOMPARE(edited.confidenceHigh, 10);
    QCOMPARE(edited.confidenceMedium, 5);
    QCOMPARE(edited.outlierIqrFactor, 1.5);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
