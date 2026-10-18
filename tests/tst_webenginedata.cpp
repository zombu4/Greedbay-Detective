// Sign out / clear cache (src/core/WebEngineData): deletes the browser data
// without starting the browser, never follows links, and refuses while a
// browser session holds the session lock.
#include "core/WebEngineData.h"
#include "core/WorkFolder.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using app::WebEngineData;
using app::WorkFolder;

class TestWebEngineData : public QObject {
    Q_OBJECT

private:
    // Creates nested files in all three browser folders.
    static bool fill(const WorkFolder &folder);
    static bool isEmptyDir(const QString &path);

private slots:
    void clearsAllThreeFolders();
    void keepsTheFoldersAndTheRest();
    void removesLinksWithoutFollowingThem();
    void refusesWhileASessionHoldsTheLock();
    void takesOverTheLockOfACrashedSession();
    void refusesWhenAFolderIsALink();
};

bool TestWebEngineData::fill(const WorkFolder &folder)
{
    const QString dirs[] = {folder.webEngineStorageDir(), folder.webEngineCacheDir(), folder.webEngineDownloadsDir()};
    for (const QString &dir : dirs) {
        if (!QDir().mkpath(dir + QStringLiteral("/sub/deeper")))
            return false;
        if (!writeTestFile(dir + QStringLiteral("/Cookies"), "c") || !writeTestFile(dir + QStringLiteral("/.hidden"), "h")
            || !writeTestFile(dir + QStringLiteral("/sub/deeper/data"), "d"))
            return false;
    }
    return true;
}

bool TestWebEngineData::isEmptyDir(const QString &path)
{
    return QFileInfo(path).isDir()
           && QDir(path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

void TestWebEngineData::clearsAllThreeFolders()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));
    QVERIFY2(WebEngineData::clear(*folder, &error), qPrintable(error));
    QVERIFY(isEmptyDir(folder->webEngineStorageDir()));
    QVERIFY(isEmptyDir(folder->webEngineCacheDir()));
    QVERIFY(isEmptyDir(folder->webEngineDownloadsDir()));
}

void TestWebEngineData::keepsTheFoldersAndTheRest()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));
    QVERIFY(writeTestFile(folder->settingsPath(), "[general]\n"));
    QVERIFY(writeTestFile(folder->path() + QStringLiteral("/webengine/other"), "x"));
    QVERIFY2(WebEngineData::clear(*folder, &error), qPrintable(error));
    QVERIFY(QFileInfo(folder->webEngineStorageDir()).isDir());
    QVERIFY(QFileInfo(folder->webEngineCacheDir()).isDir());
    QVERIFY(QFileInfo(folder->webEngineDownloadsDir()).isDir());
    QVERIFY(QFileInfo::exists(folder->settingsPath()));
    QVERIFY(QFileInfo::exists(folder->markerPath()));
    QVERIFY(QFileInfo::exists(folder->path() + QStringLiteral("/webengine/other")));
}

void TestWebEngineData::removesLinksWithoutFollowingThem()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    const QString outside = temp.path() + QStringLiteral("/outside");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeTestFile(outside + QStringLiteral("/keep.txt"), "keep"));
    QVERIFY(makeDirectoryLink(outside, folder->webEngineStorageDir() + QStringLiteral("/dirlink")));
    if (kCanLinkFiles)
        QVERIFY(QFile::link(outside + QStringLiteral("/keep.txt"), folder->webEngineCacheDir() + QStringLiteral("/filelink")));

    QVERIFY2(WebEngineData::clear(*folder, &error), qPrintable(error));
    QVERIFY(isEmptyDir(folder->webEngineStorageDir()));
    QVERIFY(isEmptyDir(folder->webEngineCacheDir()));
    QCOMPARE(readTestFile(outside + QStringLiteral("/keep.txt")), QByteArray("keep"));
}

void TestWebEngineData::refusesWhileASessionHoldsTheLock()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));

    // Another process holds the session lock, as a running browser worker does.
    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {WebEngineData::sessionLockPath(*folder), QStringLiteral("hold")});
    QVERIFY(holder.waitForStarted(30000));
    QVERIFY(holder.waitForReadyRead(30000));
    QCOMPARE(holder.readLine().trimmed(), QByteArray("locked"));

    QVERIFY(!WebEngineData::clear(*folder, &error));
    QVERIFY(error.contains(QStringLiteral("browser session is running")));
    QVERIFY(QFileInfo::exists(folder->webEngineStorageDir() + QStringLiteral("/Cookies")));

    holder.write("\n");
    QVERIFY(holder.waitForFinished(30000));
    QVERIFY2(WebEngineData::clear(*folder, &error), qPrintable(error));
    QVERIFY(isEmptyDir(folder->webEngineStorageDir()));
}

void TestWebEngineData::takesOverTheLockOfACrashedSession()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));

    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {WebEngineData::sessionLockPath(*folder), QStringLiteral("crash")});
    QVERIFY(holder.waitForFinished(30000));
    QVERIFY(QFileInfo::exists(WebEngineData::sessionLockPath(*folder)));

    QVERIFY2(WebEngineData::clear(*folder, &error), qPrintable(error));
    QVERIFY(isEmptyDir(folder->webEngineStorageDir()));
}

void TestWebEngineData::refusesWhenAFolderIsALink()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    const QString outside = temp.path() + QStringLiteral("/outside");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeTestFile(outside + QStringLiteral("/keep.txt"), "keep"));
    QVERIFY(QDir().rmdir(folder->webEngineCacheDir()));
    QVERIFY(makeDirectoryLink(outside, folder->webEngineCacheDir()));
    QVERIFY(writeTestFile(folder->webEngineStorageDir() + QStringLiteral("/Cookies"), "c"));

    QVERIFY(!WebEngineData::clear(*folder, &error));
    QVERIFY(error.contains(QStringLiteral("link")));
    // Nothing was deleted, not even in the other folders.
    QVERIFY(QFileInfo::exists(folder->webEngineStorageDir() + QStringLiteral("/Cookies")));
    QCOMPARE(readTestFile(outside + QStringLiteral("/keep.txt")), QByteArray("keep"));
}

QTEST_GUILESS_MAIN(TestWebEngineData)
#include "tst_webenginedata.moc"
