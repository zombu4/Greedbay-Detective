#include "core/WorkFolder.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using app::WorkFolder;

class TestWorkFolder : public QObject {
    Q_OBJECT

private slots:
    void defaultPathIsGenericDataPlusAppIdentifier();
    void createsLayoutAndMarker();
    void reopenKeepsMarkerId();
    void refusesMalformedMarker();
    void refusesForeignNonEmptyFolder();
    void adoptsEmptyFolder();
    void refusesRelativePath();
    void refusesLinkedFolder();
    void refusesLinkedOrMisplacedSubfolder();
    void refusesBadMarkers();
    void refusesRootThatIsAFile();
    void refusesLinkedSettingsFile();
    void refusesLinkedPageCountFile();
    void refusesLinkedKnownModelsFile();
    void restrictsFolderToOwner();
    void redirectsTempDirectory();
};

void TestWorkFolder::defaultPathIsGenericDataPlusAppIdentifier()
{
    // The real mapping, worked out with Qt's test mode off for the moment
    // (support/TestGuard.cpp turns it on for every test); only a path is
    // computed, nothing is opened or written.
    QStandardPaths::setTestModeEnabled(false);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString path = WorkFolder::defaultPath(QStringLiteral(TEST_APP_NAME));
    const QString empty = WorkFolder::defaultPath(QString());
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(!base.isEmpty());
    QCOMPARE(path, QDir::cleanPath(base + QStringLiteral("/" TEST_APP_NAME)));
#if defined(Q_OS_MACOS)
    QCOMPARE(path, QDir::homePath() + QStringLiteral("/Library/Application Support/" TEST_APP_NAME));
#elif defined(Q_OS_WIN)
    QCOMPARE(path, QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/" TEST_APP_NAME));
#else
    if (qEnvironmentVariableIsEmpty("XDG_DATA_HOME"))
        QCOMPARE(path, QDir::homePath() + QStringLiteral("/.local/share/" TEST_APP_NAME));
    else
        QCOMPARE(path, QDir::cleanPath(qEnvironmentVariable("XDG_DATA_HOME")) + QStringLiteral("/" TEST_APP_NAME));
#endif
    QVERIFY(empty.isEmpty());
}

void TestWorkFolder::createsLayoutAndMarker()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    const std::optional<WorkFolder> folder = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));

    const QStringList dirs = {folder->backupsDir(), folder->logsDir(), folder->browserProfilesDir(), folder->tmpDir(),
                              folder->pluginsDir(), folder->sharedListDir(), folder->modelsDir()};
    for (const QString &dir : dirs)
        QVERIFY2(QFileInfo(dir).isDir(), qPrintable(dir));

    QVERIFY(WorkFolder::isValidMarkerId(folder->markerId()));
    QFile marker(folder->markerPath());
    QVERIFY(marker.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromLatin1(marker.readAll()), folder->markerId() + QLatin1Char('\n'));

    QCOMPARE(folder->path(), root);
    QCOMPARE(folder->databasePath(), root + QStringLiteral("/" TEST_APP_NAME ".sqlite"));
    QCOMPARE(folder->settingsPath(), root + QStringLiteral("/settings.ini"));
    QCOMPARE(folder->pageCountPath(), root + QStringLiteral("/page-count.json"));
    QCOMPARE(folder->knownModelsPath(), root + QStringLiteral("/known-models.json"));
    QCOMPARE(folder->lockPath(), root + QStringLiteral("/.lock"));
    QCOMPARE(folder->markerPath(), root + QStringLiteral("/.workfolder"));
}

void TestWorkFolder::reopenKeepsMarkerId()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    const auto first = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(first, qPrintable(error));
    const auto second = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(second, qPrintable(error));
    QCOMPARE(second->markerId(), first->markerId());
}

void TestWorkFolder::refusesMalformedMarker()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    const auto folder = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));

    QFile marker(folder->markerPath());
    QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
    marker.write("not-a-uuid\n");
    marker.close();

    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("malformed")), qPrintable(error));

    QVERIFY(!WorkFolder::isValidMarkerId(QStringLiteral("{2f1e7a8c-0b7e-4b7e-9c3a-6f0d2a1b5c4d}")));
    QVERIFY(!WorkFolder::isValidMarkerId(QStringLiteral("2F1E7A8C-0B7E-4B7E-9C3A-6F0D2A1B5C4D")));
    QVERIFY(WorkFolder::isValidMarkerId(QStringLiteral("2f1e7a8c-0b7e-4b7e-9c3a-6f0d2a1b5c4d")));
}

void TestWorkFolder::refusesForeignNonEmptyFolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QVERIFY(QDir().mkpath(root));
    QFile other(root + QStringLiteral("/someone-elses-file.txt"));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("synthetic content\n");
    other.close();

    QString error;
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("not empty")), qPrintable(error));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/.workfolder")));
    QCOMPARE(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
}

void TestWorkFolder::adoptsEmptyFolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QVERIFY(QDir().mkpath(root));
    QString error;
    const auto folder = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));
    QVERIFY(QFileInfo::exists(folder->markerPath()));
}

void TestWorkFolder::refusesRelativePath()
{
    QString error;
    QVERIFY(!WorkFolder::open(QStringLiteral("relative/work"), QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("not absolute")), qPrintable(error));
    QVERIFY(!WorkFolder::open(QString(), QStringLiteral(TEST_APP_NAME), &error));
}

void TestWorkFolder::refusesLinkedFolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString real = tmp.path() + QStringLiteral("/real");
    const QString link = tmp.path() + QStringLiteral("/work");
    QVERIFY(QDir().mkpath(real));
    QVERIFY(makeDirectoryLink(real, link));
    QString error;
    QVERIFY(!WorkFolder::open(link, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("link")), qPrintable(error));
    QVERIFY(QDir(real).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
}

void TestWorkFolder::refusesLinkedOrMisplacedSubfolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    QVERIFY2(WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));

    // A subfolder replaced by a link to a folder elsewhere.
    const QString outside = tmp.path() + QStringLiteral("/outside");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(QDir(root + QStringLiteral("/logs")).removeRecursively());
    QVERIFY(makeDirectoryLink(outside, root + QStringLiteral("/logs")));
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("logs is a link")), qPrintable(error));

    // A subfolder replaced by a plain file.
    QTemporaryDir tmp2;
    QVERIFY(tmp2.isValid());
    const QString root2 = tmp2.path() + QStringLiteral("/work");
    QVERIFY2(WorkFolder::open(root2, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    QVERIFY(QDir(root2 + QStringLiteral("/tmp")).removeRecursively());
    QVERIFY(writeTestFile(root2 + QStringLiteral("/tmp"), "synthetic"));
    QVERIFY(!WorkFolder::open(root2, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("is not a folder")), qPrintable(error));
}

void TestWorkFolder::refusesBadMarkers()
{
    QString error;
    {
        // Too long: a valid ID followed by more text.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path() + QStringLiteral("/work");
        const auto folder = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
        QVERIFY2(folder, qPrintable(error));
        QVERIFY(writeTestFile(folder->markerPath(), folder->markerId().toLatin1() + QByteArray(100, 'x')));
        QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
        QVERIFY2(error.contains(QStringLiteral("malformed")), qPrintable(error));
    }
    {
        // The marker is a folder.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path() + QStringLiteral("/work");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/.workfolder")));
        QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    }
    if (kCanLinkFiles) {
        // The marker is a link to a valid-looking marker elsewhere.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path() + QStringLiteral("/work");
        QVERIFY(QDir().mkpath(root));
        const QString elsewhere = tmp.path() + QStringLiteral("/marker");
        QVERIFY(writeTestFile(elsewhere, "2f1e7a8c-0b7e-4b7e-9c3a-6f0d2a1b5c4d\n"));
        QVERIFY(QFile::link(elsewhere, root + QStringLiteral("/.workfolder")));
        QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
        QVERIFY2(error.contains(QStringLiteral("link")), qPrintable(error));
    }
}

void TestWorkFolder::refusesRootThatIsAFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QVERIFY(writeTestFile(root, "synthetic"));
    QString error;
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("not a folder")), qPrintable(error));
    QCOMPARE(readTestFile(root), QByteArray("synthetic"));
}

void TestWorkFolder::refusesLinkedSettingsFile()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    QVERIFY2(WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    const QString outside = tmp.path() + QStringLiteral("/outside.ini");
    QVERIFY(writeTestFile(outside, "[lookup]\n"));
    QVERIFY(QFile::link(outside, root + QStringLiteral("/settings.ini")));
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("settings.ini is a link")), qPrintable(error));
}

void TestWorkFolder::refusesLinkedPageCountFile()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    QVERIFY2(WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    const QString outside = tmp.path() + QStringLiteral("/outside.json");
    QVERIFY(writeTestFile(outside, "{}"));
    QVERIFY(QFile::link(outside, root + QStringLiteral("/page-count.json")));
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("page-count.json is a link")), qPrintable(error));
}

void TestWorkFolder::refusesLinkedKnownModelsFile()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    QVERIFY2(WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    const QString outside = tmp.path() + QStringLiteral("/outside.json");
    QVERIFY(writeTestFile(outside, "{}"));
    QVERIFY(QFile::link(outside, root + QStringLiteral("/known-models.json")));
    QVERIFY(!WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("known-models.json is a link")), qPrintable(error));
}

void TestWorkFolder::restrictsFolderToOwner()
{
#ifdef Q_OS_WIN
    QSKIP("On Windows the folder inherits the per-user permissions of %LOCALAPPDATA%");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    QVERIFY2(WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    const QFileDevice::Permissions perms = QFileInfo(root).permissions();
    const QFileDevice::Permissions others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                            | QFileDevice::ReadOther | QFileDevice::WriteOther
                                            | QFileDevice::ExeOther;
    QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
    QVERIFY(perms.testFlag(QFileDevice::WriteOwner));
    QVERIFY(perms.testFlag(QFileDevice::ExeOwner));
    QVERIFY((perms & others) == 0);
#endif
}

void TestWorkFolder::redirectsTempDirectory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/work");
    QString error;
    const auto folder = WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(folder, qPrintable(error));

#ifdef Q_OS_WIN
    const QByteArray savedTmp = qgetenv("TMP");
    const QByteArray savedTemp = qgetenv("TEMP");
#else
    const QByteArray savedTmpDir = qgetenv("TMPDIR");
#endif
    QVERIFY2(folder->redirectTempDirectory(&error), qPrintable(error));
    const QString tempPath = QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath()));
#ifdef Q_OS_WIN
    qputenv("TMP", savedTmp);
    qputenv("TEMP", savedTemp);
#else
    qputenv("TMPDIR", savedTmpDir);
#endif
    QCOMPARE(tempPath.toLower(), QDir::cleanPath(folder->tmpDir()).toLower());
}

QTEST_GUILESS_MAIN(TestWorkFolder)
#include "tst_workfolder.moc"
