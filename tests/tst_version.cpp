#include "core/Version.h"

#include <QString>
#include <QTest>

class TestVersion : public QObject {
    Q_OBJECT

private slots:
    void identifierMatchesBuildConfiguration();
    void derivedNamesFollowTheIdentifier();
    void versionStringMatchesBuildConfiguration();
    void versionStringIsBuiltFromParts();
};

void TestVersion::identifierMatchesBuildConfiguration()
{
    QCOMPARE(QString::fromLatin1(app::version::kIdentifier), QStringLiteral(TEST_APP_NAME));
    QCOMPARE(QString::fromLatin1(app::version::kDisplayName), QStringLiteral(EXPECTED_DISPLAY_NAME));
    QCOMPARE(QString::fromLatin1(app::version::kPackageName), QStringLiteral(EXPECTED_PACKAGE_NAME));
    QCOMPARE(QString::fromLatin1(app::version::kBundleId), QStringLiteral(EXPECTED_BUNDLE_ID));
}

// The derived names follow from the identifier: the display name is the
// identifier with spaces between words, the package name is the display name
// in lower case with hyphens, the bundle id is under io.github.zombu4.
void TestVersion::derivedNamesFollowTheIdentifier()
{
    const QString identifier = QString::fromLatin1(app::version::kIdentifier);
    const QString display = QString::fromLatin1(app::version::kDisplayName);
    QVERIFY(!identifier.isEmpty());
    QVERIFY(!identifier.contains(QLatin1Char(' ')));
    QCOMPARE(QString(display).remove(QLatin1Char(' ')), identifier);
    QCOMPARE(QString::fromLatin1(app::version::kPackageName),
             display.toLower().replace(QLatin1Char(' '), QLatin1Char('-')));
    QCOMPARE(QString::fromLatin1(app::version::kBundleId), QStringLiteral("io.github.zombu4.") + identifier);
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
