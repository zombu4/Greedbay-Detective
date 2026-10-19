#include "core/Settings.h"
#include "support/TestFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using app::Settings;

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
    QCOMPARE(settings.soldHistoryDays(), 90);
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
}

void TestSettings::invalidStoredValuesReadAsDefault()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString notNumber = tmp.path() + QStringLiteral("/notnumber.ini");
    QVERIFY(writeTestFile(notNumber, "[lookup]\nsoldHistoryDays=abc\n"));
    QCOMPARE(Settings(notNumber).soldHistoryDays(), 90);

    const QString negative = tmp.path() + QStringLiteral("/negative.ini");
    QVERIFY(writeTestFile(negative, "[lookup]\nsoldHistoryDays=-5\n"));
    QCOMPARE(Settings(negative).soldHistoryDays(), 90);
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
    QVERIFY(writeTestFile(path, "[pace]\npageDelayMinMs=7000\npageDelayMaxMs=2000\n"));
    const Settings edited(path);
    QCOMPARE(edited.pageDelayMinMs(), 3000);
    QCOMPARE(edited.pageDelayMaxMs(), 6000);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
