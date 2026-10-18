#include "core/Version.h"

#include <QString>
#include <QTest>

class TestVersion : public QObject {
    Q_OBJECT

private slots:
    void appNameMatchesBuildConfiguration();
    void versionStringMatchesBuildConfiguration();
    void versionStringIsBuiltFromParts();
};

void TestVersion::appNameMatchesBuildConfiguration()
{
    QCOMPARE(QString::fromLatin1(app::version::kAppName), QStringLiteral(EXPECTED_APP_NAME));
}

void TestVersion::versionStringMatchesBuildConfiguration()
{
    QCOMPARE(QString::fromLatin1(app::version::kString), QStringLiteral(EXPECTED_VERSION));
}

void TestVersion::versionStringIsBuiltFromParts()
{
    const QString joined = QStringLiteral("%1.%2.%3")
                               .arg(app::version::kMajor)
                               .arg(app::version::kMinor)
                               .arg(app::version::kPatch);
    QCOMPARE(QString::fromLatin1(app::version::kString), joined);
}

QTEST_GUILESS_MAIN(TestVersion)
#include "tst_version.moc"
