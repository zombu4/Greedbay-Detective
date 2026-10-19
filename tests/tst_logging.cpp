#include "core/Logging.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using app::Logging;

class TestLogging : public QObject {
    Q_OBJECT

private slots:
    void cleanup();

    void writesFormattedLines();
    void linesEndInNewlineOnly();
    void installingTwiceLogsEachMessageOnce();
    void rotatesWhenTheNextLineWouldExceedTheLimit();
    void cutsMessagesLongerThanTheLineLimit();
    void rotateShiftsFilesAndDeletesOnlyTheOldest();
    void failedRotationLosesNothingAndIsNotRepeated();
    void rotateRefusesLinks();
    void refusesALinkedLogFile();
    void fileNames();
};

void TestLogging::cleanup()
{
    Logging::uninstall();
}

void TestLogging::writesFormattedLines()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString error;
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    qWarning("synthetic warning %d", 42);
    qInfo("synthetic info");
    Logging::uninstall();

    const QList<QByteArray> lines = readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log")).trimmed().split('\n');
    QCOMPARE(lines.size(), 2);
    const QRegularExpression pattern(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z (\\w+) \\[default\\] (.*)$"));
    const QRegularExpressionMatch first = pattern.match(QString::fromUtf8(lines.at(0)));
    QVERIFY2(first.hasMatch(), lines.at(0).constData());
    QCOMPARE(first.captured(1), QStringLiteral("warning"));
    QCOMPARE(first.captured(2), QStringLiteral("synthetic warning 42"));
    const QRegularExpressionMatch second = pattern.match(QString::fromUtf8(lines.at(1)));
    QVERIFY2(second.hasMatch(), lines.at(1).constData());
    QCOMPARE(second.captured(1), QStringLiteral("info"));

    // After uninstall nothing more is written.
    qInfo("not logged");
    QCOMPARE(readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log")).trimmed().split('\n').size(), 2);
}

void TestLogging::linesEndInNewlineOnly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString error;
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    qInfo("one");
    qInfo("two");
    Logging::uninstall();
    const QByteArray content = readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log"));
    QVERIFY(!content.contains('\r'));
    QCOMPARE(content.count('\n'), 2);
}

void TestLogging::installingTwiceLogsEachMessageOnce()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString error;
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    qInfo("exactly once");
    Logging::uninstall();
    QCOMPARE(readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log")).count("exactly once"), 1);
}

void TestLogging::rotatesWhenTheNextLineWouldExceedTheLimit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString current = tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log");
    const QByteArray filler(Logging::kMaxFileBytes - 10, 'x');
    QVERIFY(writeTestFile(current, filler));

    QString error;
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    qWarning("this line does not fit into the remaining 10 bytes");
    Logging::uninstall();

    QCOMPARE(readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".1.log")), filler);
    const QByteArray now = readTestFile(current);
    QVERIFY(now.contains("this line does not fit"));
    QVERIFY(now.size() < 200);
}

void TestLogging::cutsMessagesLongerThanTheLineLimit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString error;
    // Logging passes each message on to the handler installed before it;
    // here that would be QtTest's, printing the whole message as one line of
    // 2 MiB. Every CI log stopped right before that line and the test step
    // never ended (seen on all four runners, run 39186908428), so a silent
    // handler stands in for this test. The log file is what is checked.
    const QtMessageHandler testHandler =
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QVERIFY2(Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    qInfo("%s", QByteArray(Logging::kMaxFileBytes * 2, 'y').constData());
    Logging::uninstall();
    qInstallMessageHandler(testHandler);
    const QByteArray content = readTestFile(tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log"));
    QVERIFY(content.size() <= Logging::kMaxLineBytes);
    QVERIFY(content.endsWith(" [cut]\n"));
}

void TestLogging::rotateShiftsFilesAndDeletesOnlyTheOldest()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path();
    QVERIFY(writeTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".log"), "0"));
    for (int i = 1; i <= 4; ++i)
        QVERIFY(writeTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".%1.log").arg(i), QByteArray::number(i)));
    QVERIFY(writeTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".5.log"), "not ours"));
    QVERIFY(writeTestFile(dir + QStringLiteral("/other.txt"), "unrelated"));

    QString error;
    QVERIFY2(Logging::rotate(dir, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));

    QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/" TEST_APP_NAME ".log")));
    QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".1.log")), QByteArray("0"));
    QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".2.log")), QByteArray("1"));
    QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".3.log")), QByteArray("2"));
    QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".4.log")), QByteArray("3"));
    QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".5.log")), QByteArray("not ours"));
    QCOMPARE(readTestFile(dir + QStringLiteral("/other.txt")), QByteArray("unrelated"));
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden).size(), 6);
}

// On Unix a folder without write permission makes every rename fail, which
// stands in for a log file another program holds open on Windows.
void TestLogging::failedRotationLosesNothingAndIsNotRepeated()
{
#ifdef Q_OS_WIN
    QSKIP("Folder permissions cannot block renames for the owner on Windows");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/logs");
    QVERIFY(QDir().mkpath(dir));
    const QByteArray filler(Logging::kMaxFileBytes - 10, 'x');
    QVERIFY(writeTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".log"), filler));
    for (int i = 1; i <= 4; ++i)
        QVERIFY(writeTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".%1.log").arg(i), QByteArray::number(i)));

    QString error;
    QVERIFY2(Logging::install(dir, QStringLiteral(TEST_APP_NAME), &error), qPrintable(error));
    QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    if (permissionsIgnored(dir)) {
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QSKIP("This process ignores folder permissions (for example root)");
    }
    for (int i = 0; i < 5; ++i)
        qWarning("message %d after the folder became read-only", i);
    Logging::uninstall();
    QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    for (int i = 1; i <= 4; ++i)
        QCOMPARE(readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".%1.log").arg(i)), QByteArray::number(i));
    const QByteArray current = readTestFile(dir + QStringLiteral("/" TEST_APP_NAME ".log"));
    QVERIFY(current.startsWith(filler));
    QCOMPARE(current.count("after the folder became read-only"), 5);
#endif
}

void TestLogging::rotateRefusesLinks()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.path() + QStringLiteral("/outside.txt");
    QVERIFY(writeTestFile(target, "must survive"));
    QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/logs")));
    QVERIFY(writeTestFile(tmp.path() + QStringLiteral("/logs/" TEST_APP_NAME ".log"), "current"));
    QVERIFY(QFile::link(target, tmp.path() + QStringLiteral("/logs/" TEST_APP_NAME ".4.log")));

    QString error;
    QVERIFY(!Logging::rotate(tmp.path() + QStringLiteral("/logs"), QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("not a regular file")), qPrintable(error));
    QCOMPARE(readTestFile(target), QByteArray("must survive"));
    QCOMPARE(readTestFile(tmp.path() + QStringLiteral("/logs/" TEST_APP_NAME ".log")), QByteArray("current"));
}

void TestLogging::refusesALinkedLogFile()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.path() + QStringLiteral("/outside.txt");
    QVERIFY(writeTestFile(target, "outside"));
    QVERIFY(QFile::link(target, tmp.path() + QStringLiteral("/" TEST_APP_NAME ".log")));
    QString error;
    QVERIFY(!Logging::install(tmp.path(), QStringLiteral(TEST_APP_NAME), &error));
    QVERIFY2(error.contains(QStringLiteral("link")), qPrintable(error));
    QCOMPARE(readTestFile(target), QByteArray("outside"));
}

void TestLogging::fileNames()
{
    QCOMPARE(Logging::currentFileName(QStringLiteral(TEST_APP_NAME)), QStringLiteral(TEST_APP_NAME ".log"));
    QCOMPARE(Logging::rotatedFileName(QStringLiteral(TEST_APP_NAME), 3), QStringLiteral(TEST_APP_NAME ".3.log"));
    QCOMPARE(Logging::kMaxFiles, 5);
    QCOMPARE(Logging::kMaxFileBytes, qint64(1024 * 1024));
}

QTEST_GUILESS_MAIN(TestLogging)
#include "tst_logging.moc"
