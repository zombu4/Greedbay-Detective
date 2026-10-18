#include "core/SingleInstance.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using app::SingleInstance;

class TestSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void acquireAndReleaseInOneProcess();
    void refusedWhileAnotherProcessHoldsIt();
    void lockLeftByCrashedProcessIsTakenOver();
};

void TestSingleInstance::acquireAndReleaseInOneProcess()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString lockPath = tmp.path() + QStringLiteral("/.lock");

    SingleInstance first(lockPath);
    QVERIFY2(first.tryAcquire(), qPrintable(first.error()));
    QVERIFY(first.isHeld());
    QVERIFY(first.tryAcquire()); // already held: still true

    SingleInstance second(lockPath);
    QVERIFY(!second.tryAcquire());
    QVERIFY(!second.error().isEmpty());

    first.release();
    QVERIFY(!first.isHeld());
    QVERIFY2(second.tryAcquire(), qPrintable(second.error()));
    second.release();
    QVERIFY(!QFile::exists(lockPath));
}

void TestSingleInstance::refusedWhileAnotherProcessHoldsIt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString lockPath = tmp.path() + QStringLiteral("/.lock");

    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {lockPath, QStringLiteral("hold")});
    QVERIFY(holder.waitForStarted());
    QVERIFY(holder.waitForReadyRead(10000));
    QCOMPARE(holder.readLine().trimmed(), QByteArray("locked"));

    SingleInstance mine(lockPath);
    QVERIFY(!mine.tryAcquire());
    QCOMPARE(mine.error(), QStringLiteral("another instance is using this work folder"));

    holder.write("quit\n");
    holder.closeWriteChannel();
    QVERIFY(holder.waitForFinished(10000));
    QCOMPARE(holder.exitCode(), 0);

    QVERIFY2(mine.tryAcquire(), qPrintable(mine.error()));
}

void TestSingleInstance::lockLeftByCrashedProcessIsTakenOver()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString lockPath = tmp.path() + QStringLiteral("/.lock");

    QProcess holder;
    holder.start(QStringLiteral(LOCKHOLDER_PATH), {lockPath, QStringLiteral("crash")});
    QVERIFY(holder.waitForFinished(10000));
    QCOMPARE(holder.readAllStandardOutput().trimmed(), QByteArray("locked"));
    QVERIFY(QFile::exists(lockPath)); // the crashed process left its lock file behind

    SingleInstance mine(lockPath);
    QVERIFY2(mine.tryAcquire(), qPrintable(mine.error()));
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
