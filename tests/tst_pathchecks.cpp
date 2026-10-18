#include "core/PathChecks.h"
#include "support/TestFiles.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

class TestPathChecks : public QObject {
    Q_OBJECT

private slots:
    void plainFileAndFolder();
    void folderLink();
    void danglingFolderLink();
    void fileLink();
    void nothing();
};

void TestPathChecks::plainFileAndFolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString file = tmp.path() + QStringLiteral("/plain.txt");
    QVERIFY(writeTestFile(file, "synthetic"));
    QVERIFY(app::isPlainFile(file));
    QVERIFY(!app::isLink(file));
    QVERIFY(app::somethingAt(file));
    QVERIFY(!app::isPlainFile(tmp.path()));
    QVERIFY(!app::isLink(tmp.path()));
    QVERIFY(app::somethingAt(tmp.path()));
}

void TestPathChecks::folderLink()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.path() + QStringLiteral("/target");
    const QString link = tmp.path() + QStringLiteral("/link");
    QVERIFY(QDir().mkpath(target));
    QVERIFY(makeDirectoryLink(target, link));
    QVERIFY(app::isLink(link));
    QVERIFY(!app::isPlainFile(link));
    QVERIFY(app::somethingAt(link));
}

void TestPathChecks::danglingFolderLink()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.path() + QStringLiteral("/target");
    const QString link = tmp.path() + QStringLiteral("/link");
    QVERIFY(QDir().mkpath(target));
    QVERIFY(makeDirectoryLink(target, link));
    QVERIFY(QDir(target).removeRecursively());
    // A link whose target is gone still counts as something being there, so
    // no code treats its name as free.
    QVERIFY(app::isLink(link));
    QVERIFY(app::somethingAt(link));
    QVERIFY(!app::isPlainFile(link));
}

void TestPathChecks::fileLink()
{
    if (!kCanLinkFiles)
        QSKIP("File links need extra privileges on Windows; folder junctions are tested instead");
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.path() + QStringLiteral("/target.txt");
    const QString link = tmp.path() + QStringLiteral("/link.txt");
    QVERIFY(writeTestFile(target, "synthetic"));
    QVERIFY(QFile::link(target, link));
    QVERIFY(app::isLink(link));
    QVERIFY(!app::isPlainFile(link));
    QVERIFY(QFile::remove(target));
    QVERIFY(app::somethingAt(link));
}

void TestPathChecks::nothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString missing = tmp.path() + QStringLiteral("/missing");
    QVERIFY(!app::somethingAt(missing));
    QVERIFY(!app::isLink(missing));
    QVERIFY(!app::isPlainFile(missing));
}

QTEST_GUILESS_MAIN(TestPathChecks)
#include "tst_pathchecks.moc"
