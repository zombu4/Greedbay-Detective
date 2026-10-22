// Browser profiles in the work folder (src/core/BrowserProfiles): paths, ids,
// and sign-out, which deletes one browser's profile folder without starting
// the browser, never follows links, and refuses while the app's instance of
// that browser runs (its lock is held).
#include "core/BrowserProfiles.h"
#include "core/WorkFolder.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using app::BrowserProfiles;
using app::WorkFolder;

class TestBrowserProfiles : public QObject {
    Q_OBJECT

private:
    // A work folder in `temp` with a filled profile folder for "brave".
    static std::optional<WorkFolder> filled(const QTemporaryDir &temp);
    static bool present(const QString &path);

private slots:
    void paths();
    void ids_data();
    void ids();
    void clearDeletesTheProfileFolder();
    void clearWithoutProfileSucceeds();
    void clearKeepsOtherBrowsersAndTheWorkFolder();
    void removesLinksWithoutFollowingThem();
    void refusesWhileTheBrowserRuns();
    void takesOverTheLockOfACrashedApp();
    void refusesWhenTheProfileIsALink();
    void refusesInvalidIds();
};

std::optional<WorkFolder> TestBrowserProfiles::filled(const QTemporaryDir &temp)
{
    QString error;
    std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    if (!folder)
        return std::nullopt;
    const QString dir = BrowserProfiles::profileDir(*folder, QStringLiteral("brave"));
    if (!QDir().mkpath(dir + QStringLiteral("/Default/Local Storage")) || !writeTestFile(dir + QStringLiteral("/Default/Cookies"), "c")
        || !writeTestFile(dir + QStringLiteral("/Local State"), "s")
        || !writeTestFile(dir + QStringLiteral("/Default/Local Storage/data"), "d"))
        return std::nullopt;
    return folder;
}

bool TestBrowserProfiles::present(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

void TestBrowserProfiles::paths()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QCOMPARE(BrowserProfiles::profileDir(*folder, QStringLiteral("chrome")), folder->path() + QStringLiteral("/browser/chrome"));
    QCOMPARE(BrowserProfiles::lockPath(*folder, QStringLiteral("chrome")), folder->path() + QStringLiteral("/browser/chrome.lock"));
    QVERIFY(QFileInfo(folder->browserProfilesDir()).isDir());
}

void TestBrowserProfiles::ids_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<bool>("valid");
    QTest::newRow("brave") << "brave" << true;
    QTest::newRow("chromium") << "chromium" << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("upper case") << "Chrome" << false;
    QTest::newRow("parent path") << "../x" << false;
    QTest::newRow("slash") << "a/b" << false;
    QTest::newRow("dot") << "." << false;
    QTest::newRow("digit") << "chrome2" << false;
}

void TestBrowserProfiles::ids()
{
    QFETCH(QString, id);
    QFETCH(bool, valid);
    QCOMPARE(BrowserProfiles::isValidId(id), valid);
}

void TestBrowserProfiles::clearDeletesTheProfileFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    QString error;
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
    QVERIFY(!present(BrowserProfiles::profileDir(*folder, QStringLiteral("brave"))));
}

void TestBrowserProfiles::clearWithoutProfileSucceeds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("edge"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
}

void TestBrowserProfiles::clearKeepsOtherBrowsersAndTheWorkFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    const QString chrome = BrowserProfiles::profileDir(*folder, QStringLiteral("chrome"));
    QVERIFY(QDir().mkpath(chrome));
    QVERIFY(writeTestFile(chrome + QStringLiteral("/Local State"), "s"));
    QVERIFY(writeTestFile(folder->settingsPath(), "[general]\n"));
    QString error;
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
    QVERIFY(present(chrome + QStringLiteral("/Local State")));
    QVERIFY(present(folder->settingsPath()));
    QVERIFY(present(folder->markerPath()));
    QVERIFY(QFileInfo(folder->browserProfilesDir()).isDir());
}

void TestBrowserProfiles::removesLinksWithoutFollowingThem()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    const QString outside = temp.path() + QStringLiteral("/outside");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeTestFile(outside + QStringLiteral("/keep.txt"), "keep"));
    const QString dir = BrowserProfiles::profileDir(*folder, QStringLiteral("brave"));
    QVERIFY(makeDirectoryLink(outside, dir + QStringLiteral("/dirlink")));
    if (kCanLinkFiles)
        QVERIFY(QFile::link(outside + QStringLiteral("/keep.txt"), dir + QStringLiteral("/filelink")));
    QString error;
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
    QVERIFY(!present(dir));
    QCOMPARE(readTestFile(outside + QStringLiteral("/keep.txt")), QByteArray("keep"));
}

void TestBrowserProfiles::refusesWhileTheBrowserRuns()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    // Another process holds the lock, as the app does while its browser runs.
    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {BrowserProfiles::lockPath(*folder, QStringLiteral("brave")), QStringLiteral("hold")});
    QVERIFY(holder.waitForStarted(30000));
    QVERIFY(holder.waitForReadyRead(30000));
    QCOMPARE(holder.readLine().trimmed(), QByteArray("locked"));
    QString error;
    QVERIFY(!BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error));
    QVERIFY(error.contains(QStringLiteral("still running")));
    QVERIFY(present(BrowserProfiles::profileDir(*folder, QStringLiteral("brave")) + QStringLiteral("/Default/Cookies")));
    holder.write("\n");
    QVERIFY(holder.waitForFinished(30000));
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
    QVERIFY(!present(BrowserProfiles::profileDir(*folder, QStringLiteral("brave"))));
}

void TestBrowserProfiles::takesOverTheLockOfACrashedApp()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {BrowserProfiles::lockPath(*folder, QStringLiteral("brave")), QStringLiteral("crash")});
    QVERIFY(holder.waitForFinished(30000));
    QVERIFY(present(BrowserProfiles::lockPath(*folder, QStringLiteral("brave"))));
    QString error;
    QVERIFY2(BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error), qPrintable(error));
    QVERIFY(!present(BrowserProfiles::profileDir(*folder, QStringLiteral("brave"))));
}

void TestBrowserProfiles::refusesWhenTheProfileIsALink()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    const QString outside = temp.path() + QStringLiteral("/outside");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeTestFile(outside + QStringLiteral("/Cookies"), "c"));
    QVERIFY(makeDirectoryLink(outside, BrowserProfiles::profileDir(*folder, QStringLiteral("brave"))));
    QVERIFY(!BrowserProfiles::clear(*folder, QStringLiteral("brave"), app::DeleteGrants::confirmedSignOut(), &error));
    QVERIFY(error.contains(QStringLiteral("link")));
    QCOMPARE(readTestFile(outside + QStringLiteral("/Cookies")), QByteArray("c"));
}

void TestBrowserProfiles::refusesInvalidIds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const std::optional<WorkFolder> folder = filled(temp);
    QVERIFY(folder);
    QString error;
    QVERIFY(!BrowserProfiles::clear(*folder, QStringLiteral(".."), app::DeleteGrants::confirmedSignOut(), &error));
    QVERIFY(!BrowserProfiles::clear(*folder, QStringLiteral(""), app::DeleteGrants::confirmedSignOut(), &error));
    QVERIFY(present(folder->browserProfilesDir()));
    QVERIFY(present(BrowserProfiles::profileDir(*folder, QStringLiteral("brave")) + QStringLiteral("/Default/Cookies")));
}

QTEST_GUILESS_MAIN(TestBrowserProfiles)
#include "tst_browserprofiles.moc"
