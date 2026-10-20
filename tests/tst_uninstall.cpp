// Uninstall of the user's data (core/Uninstall, PLAN 9): keep or delete,
// links never followed, and the safety checks. Only the paths the test made
// are looked at; nothing is searched for. Temporary work folders have no
// known paths (those belong to the app's default folder only), and the one
// default folder the test uses has an identifier of its own.
#include "core/Uninstall.h"
#include "core/WorkFolder.h"
#include "support/TestFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using app::Uninstall;
using app::WorkFolder;

namespace {

bool present(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

// Every kind of entry the app keeps in its work folder.
bool fill(const WorkFolder &folder)
{
    const QString root = folder.path();
    return writeTestFile(folder.databasePath(), "db") && writeTestFile(folder.databasePath() + "-journal", "hot journal")
           && writeTestFile(folder.backupsDir() + "/old.sqlite", "backup")
           && writeTestFile(folder.logsDir() + "/app.log", "log") && writeTestFile(folder.settingsPath(), "[a]\nb=1\n")
           && writeTestFile(folder.pageCountPath(), "{}") && writeTestFile(folder.knownModelsPath(), "{\"format\":1}")
           && QDir().mkpath(folder.browserProfilesDir() + "/brave/Default")
           && writeTestFile(folder.browserProfilesDir() + "/brave/Default/Cookies", "cookies")
           && QDir().mkpath(folder.pluginsDir() + "/mine") && writeTestFile(folder.pluginsDir() + "/mine/main.js", "x")
           && writeTestFile(folder.tmpDir() + "/scratch", "tmp") && !root.isEmpty();
}

} // namespace

class TestUninstall : public QObject {
    Q_OBJECT

private slots:
    void keepsTheHistory();
    void deletesEverything();
    void neverFollowsLinks();
    void checksBeforeDeleting();
};

void TestUninstall::keepsTheHistory()
{
    QTemporaryDir temp;
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + "/work", QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));
    const Uninstall::Report report = Uninstall::run(*folder, true);
    QVERIFY2(report.ok(), qPrintable(report.failed.join('\n')));
    // Kept: the database and its journal, its backups, the marker, the
    // confirmed models.
    QVERIFY(present(folder->databasePath()));
    QVERIFY(present(folder->databasePath() + "-journal"));
    QVERIFY(present(folder->backupsDir() + "/old.sqlite"));
    QVERIFY(present(folder->markerPath()));
    QVERIFY(present(folder->knownModelsPath()));
    // Gone: everything else.
    for (const QString &path : {folder->logsDir(), folder->settingsPath(), folder->pageCountPath(), folder->browserProfilesDir(),
                                folder->pluginsDir(), folder->tmpDir()})
        QVERIFY2(!present(path), qPrintable(path));
    QCOMPARE(report.kept.size(), 5);
    QVERIFY(!present(folder->lockPath()));
    QVERIFY(report.removed.contains(QDir::toNativeSeparators(folder->browserProfilesDir())));
    // A later start reuses the kept folder.
    QVERIFY2(WorkFolder::open(folder->path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
}

void TestUninstall::deletesEverything()
{
    QTemporaryDir temp;
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + "/work", QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(fill(*folder));
    const Uninstall::Report report = Uninstall::run(*folder, false);
    QVERIFY2(report.ok(), qPrintable(report.failed.join('\n')));
    QVERIFY(!present(folder->path()));
    QVERIFY(report.kept.isEmpty());
    QVERIFY(report.removed.contains(QDir::toNativeSeparators(folder->path())));
}

void TestUninstall::neverFollowsLinks()
{
    QTemporaryDir temp;
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(temp.path() + "/work", QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));
    // A folder outside the work folder, linked from inside a profile.
    const QString outside = temp.path() + "/outside";
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeTestFile(outside + "/precious.txt", "keep me"));
    QVERIFY(QDir().mkpath(folder->browserProfilesDir() + "/brave"));
    QVERIFY(makeDirectoryLink(outside, folder->browserProfilesDir() + "/brave/linked"));
    const Uninstall::Report report = Uninstall::run(*folder, false);
    QVERIFY2(report.ok(), qPrintable(report.failed.join('\n')));
    QVERIFY(!present(folder->path()));
    QCOMPARE(readTestFile(outside + "/precious.txt"), QByteArray("keep me"));
}

void TestUninstall::checksBeforeDeleting()
{
    // A folder that is not the default work folder is refused.
    QTemporaryDir temp;
    QString error;
    const std::optional<WorkFolder> elsewhere = WorkFolder::open(temp.path() + "/work", QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(elsewhere, qPrintable(error));
    QVERIFY(Uninstall::check(*elsewhere).contains("not the app's own work folder"));

    // The default folder of an identifier only this test uses (never the
    // app's own): accepted, refused while its lock is held, refused without
    // a valid marker.
    const QString identifier = QStringLiteral("%1UninstallTest%2").arg(QStringLiteral(TEST_APP_NAME)).arg(QCoreApplication::applicationPid());
    const QString path = WorkFolder::defaultPath(identifier);
    QVERIFY(!path.isEmpty());
    QVERIFY2(!present(path), qPrintable(path));
    std::optional<WorkFolder> folder = WorkFolder::open(path, identifier, &error);
    QVERIFY2(folder, qPrintable(error));
    QCOMPARE(Uninstall::check(*folder), QString());

    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {folder->lockPath(), QStringLiteral("hold")});
    QVERIFY(holder.waitForStarted(30000));
    QVERIFY(holder.waitForReadyRead(30000));
    QCOMPARE(holder.readLine().trimmed(), QByteArray("locked"));
    QVERIFY(Uninstall::check(*folder).contains("running"));
    // run() takes the lock itself: while the app holds it, nothing goes.
    const Uninstall::Report refused = Uninstall::run(*folder, false);
    QVERIFY(!refused.ok());
    QVERIFY(refused.removed.isEmpty());
    QVERIFY(present(folder->markerPath()));
    holder.write("\n");
    QVERIFY(holder.waitForFinished(30000));

    QVERIFY(writeTestFile(folder->markerPath(), "not an id\n"));
    QVERIFY(Uninstall::check(*folder).contains("marker"));

    // Clean up through run(): the test's own default folder, and on macOS its
    // cache folder, are gone afterwards.
    const Uninstall::Report report = Uninstall::run(*folder, false);
    QVERIFY2(report.ok(), qPrintable(report.failed.join('\n')));
    QVERIFY(!present(path));
    for (const QString &known : Uninstall::knownPaths(identifier))
        QVERIFY2(!present(known), qPrintable(known));
}

QTEST_GUILESS_MAIN(TestUninstall)
#include "tst_uninstall.moc"
