// Browser-control logic that needs no browser: DevTools message parsing, the
// browser's /json/version answer, the outcomes decided from a URL, extractor result
// checking, and the browser choice. On the CI runners it also checks that
// the browsers the runner images include are found.
#include "browser/BrowserCatalog.h"
#include "browser/CdpConnection.h"
#include "browser/Extractor.h"

#include <QTest>

using app::browser::BrowserCatalog;
using app::browser::CdpConnection;
using app::browser::Extractor;
using app::browser::InstalledBrowser;
using app::ebay::PageKind;

class TestBrowserLogic : public QObject {
    Q_OBJECT

private slots:
    void parseMessage();
    void versionAnswer_data();
    void versionAnswer();
    void outcomeFromUrl();
    void parseResultRejectsInvalidResults();
    void scriptCarriesTheInput();
    void preferenceOrder();
    void choose();
    void runnerBrowsersAreFound();
};

void TestBrowserLogic::parseMessage()
{
    QString error;
    const std::optional<QJsonObject> ok = CdpConnection::parseMessage(R"({"id":3,"result":{}})", &error);
    QVERIFY(ok);
    QCOMPARE(ok->value("id").toInt(), 3);
    QVERIFY(!CdpConnection::parseMessage("[1,2]", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!CdpConnection::parseMessage("not json", &error));
}

void TestBrowserLogic::versionAnswer_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("url");
    QTest::newRow("valid") << QByteArray(R"({"Browser":"Chrome/155.0.0.0","webSocketDebuggerUrl":"ws://127.0.0.1:54321/devtools/browser/0a1b-2c3d"})")
                           << "ws://127.0.0.1:54321/devtools/browser/0a1b-2c3d";
    QTest::newRow("other port") << QByteArray(R"({"webSocketDebuggerUrl":"ws://127.0.0.1:9222/devtools/browser/abc"})") << "";
    QTest::newRow("other host") << QByteArray(R"({"webSocketDebuggerUrl":"ws://192.0.2.1:54321/devtools/browser/abc"})") << "";
    QTest::newRow("localhost name") << QByteArray(R"({"webSocketDebuggerUrl":"ws://localhost:54321/devtools/browser/abc"})") << "";
    QTest::newRow("other path") << QByteArray(R"({"webSocketDebuggerUrl":"ws://127.0.0.1:54321/devtools/page/abc"})") << "";
    QTest::newRow("query") << QByteArray(R"({"webSocketDebuggerUrl":"ws://127.0.0.1:54321/devtools/browser/abc?x=1"})") << "";
    QTest::newRow("wss") << QByteArray(R"({"webSocketDebuggerUrl":"wss://127.0.0.1:54321/devtools/browser/abc"})") << "";
    QTest::newRow("missing") << QByteArray(R"({"Browser":"x"})") << "";
    QTest::newRow("not json") << QByteArray("nope") << "";
}

void TestBrowserLogic::versionAnswer()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, url);
    QCOMPARE(CdpConnection::webSocketUrlFromVersion(json, 54321).toString(), url);
}

void TestBrowserLogic::outcomeFromUrl()
{
    const QUrl landing(QStringLiteral("https://www.ebay.com/sh/landing"));
    const QUrl signIn(QStringLiteral("https://signin.ebay.com/ws/eBayISAPI.dll?SignIn&ru=https%3A%2F%2Fwww.ebay.com%2F"));
    QCOMPARE(Extractor::outcomeFromUrl(landing, PageKind::Research)->value("page").toString(), QStringLiteral("no_access"));
    QVERIFY(!Extractor::outcomeFromUrl(landing, PageKind::Item).has_value()); // read the page: unexpected there
    QCOMPARE(Extractor::outcomeFromUrl(signIn, PageKind::ActiveSearch)->value("page").toString(), QStringLiteral("sign_in"));
    QVERIFY(!Extractor::outcomeFromUrl(QUrl(QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=x")), PageKind::ActiveSearch));
}

void TestBrowserLogic::parseResultRejectsInvalidResults()
{
    QCOMPARE(Extractor::parseResult(QJsonValue(42)).value("page").toString(), QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QJsonValue()).value("page").toString(), QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QJsonValue(QStringLiteral("not json"))).value("page").toString(), QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QJsonValue(QStringLiteral("[1,2]"))).value("page").toString(), QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QJsonValue(QStringLiteral("{\"page\":\"something\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QJsonValue(QStringLiteral("{\"page\":\"expected\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    // A page script cannot claim the URL-based outcomes.
    QCOMPARE(Extractor::parseResult(QJsonValue(QStringLiteral("{\"page\":\"no_access\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    const QJsonObject ok = Extractor::parseResult(QJsonValue(QStringLiteral("{\"page\":\"expected\",\"data\":{\"a\":1}}")));
    QCOMPARE(ok.value("page").toString(), QStringLiteral("expected"));
    QCOMPARE(ok.value("data").toObject().value("a").toInt(), 1);
}

void TestBrowserLogic::scriptCarriesTheInput()
{
    const QString script = Extractor::script(PageKind::Item, QUrl(QStringLiteral("https://www.ebay.com/itm/100000000001")));
    QVERIFY(script.contains(QStringLiteral("\"kind\":\"item\"")));
    QVERIFY(script.contains(QStringLiteral("\"itemId\":\"100000000001\"")));
    QVERIFY(script.contains(QStringLiteral("function extract(input)")));
    QVERIFY(script.contains(QStringLiteral("function isErrorPage()")));
}

void TestBrowserLogic::preferenceOrder()
{
#if defined(Q_OS_LINUX)
    QCOMPARE(BrowserCatalog::preferenceOrder(),
             QStringList({QStringLiteral("brave"), QStringLiteral("chrome"), QStringLiteral("edge"), QStringLiteral("chromium")}));
#else
    QCOMPARE(BrowserCatalog::preferenceOrder(),
             QStringList({QStringLiteral("brave"), QStringLiteral("chrome"), QStringLiteral("edge")}));
#endif
}

void TestBrowserLogic::choose()
{
    const QList<InstalledBrowser> list{{QStringLiteral("chrome"), QStringLiteral("Google Chrome"), QStringLiteral("/x/chrome")},
                                       {QStringLiteral("edge"), QStringLiteral("Microsoft Edge"), QStringLiteral("/x/edge")}};
    QCOMPARE(BrowserCatalog::choose(list, QString())->id, QStringLiteral("chrome"));
    QCOMPARE(BrowserCatalog::choose(list, QStringLiteral("edge"))->id, QStringLiteral("edge"));
    // A picked browser that is not installed: the first installed one.
    QCOMPARE(BrowserCatalog::choose(list, QStringLiteral("brave"))->id, QStringLiteral("chrome"));
    QVERIFY(!BrowserCatalog::choose({}, QStringLiteral("chrome")).has_value());
}

void TestBrowserLogic::runnerBrowsersAreFound()
{
    const QList<InstalledBrowser> found = BrowserCatalog::installed();
    QStringList ids;
    for (const InstalledBrowser &browser : found)
        ids << browser.id + QStringLiteral(" ") + browser.executable;
    qInfo().noquote() << "installed browsers:" << (ids.isEmpty() ? QStringLiteral("none") : ids.join(QStringLiteral(", ")));
    if (qEnvironmentVariable("GITHUB_ACTIONS") != QLatin1String("true"))
        QSKIP("not on a GitHub Actions runner; the expected browsers are known only there");
#if defined(Q_OS_MACOS) && defined(Q_PROCESSOR_X86_64)
    QSKIP("macos-26-intel: no software list for this runner image was found, so no browser is expected");
#endif
    // The runner images list Google Chrome and Microsoft Edge on macOS 26,
    // Windows 2025 and Ubuntu 24.04 (github.com/actions/runner-images), and
    // Chromium on Ubuntu 24.04. Finding them proves the lookup per system.
    QStringList foundIds;
    for (const InstalledBrowser &browser : found)
        foundIds << browser.id;
    QVERIFY2(foundIds.contains(QStringLiteral("chrome")), "Google Chrome not found");
    QVERIFY2(foundIds.contains(QStringLiteral("edge")), "Microsoft Edge not found");
#if defined(Q_OS_LINUX)
    QVERIFY2(foundIds.contains(QStringLiteral("chromium")), "Chromium not found");
#endif
}

QTEST_GUILESS_MAIN(TestBrowserLogic)
#include "tst_browserlogic.moc"
