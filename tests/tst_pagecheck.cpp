// URL rules for eBay pages (src/ebay/PageCheck): classification, the main
// page allow list, item numbers and log-safe URLs.
#include "ebay/PageCheck.h"
#include "ebay/PageKind.h"

#include <QTest>

using namespace app::ebay;

class TestPageCheck : public QObject {
    Q_OBJECT

private slots:
    void classify_data();
    void classify();
    void signInStartIsAnAccountPage();
    void allowedMainFrame_data();
    void allowedMainFrame();
    void itemId_data();
    void itemId();
    void logUrlDropsQueryAndCredentials();
    void pageKindNames();
};

void TestPageCheck::classify_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<int>("expected");
    QTest::newRow("sign-in") << "https://signin.ebay.com/ws/eBayISAPI.dll?SignIn&ru=x" << int(UrlClass::SignIn);
    QTest::newRow("sign-in upper case host") << "https://SIGNIN.EBAY.COM/ws/x" << int(UrlClass::SignIn);
    QTest::newRow("landing") << "https://www.ebay.com/sh/landing" << int(UrlClass::ResearchLanding);
    QTest::newRow("landing subpath") << "https://www.ebay.com/sh/landing/x?y=1" << int(UrlClass::ResearchLanding);
    QTest::newRow("landing prefix only") << "https://www.ebay.com/sh/landingpage" << int(UrlClass::Other);
    QTest::newRow("my ebay") << "https://www.ebay.com/mye/myebay/summary" << int(UrlClass::AccountPage);
    QTest::newRow("research") << "https://www.ebay.com/sh/research?keywords=x" << int(UrlClass::Other);
    QTest::newRow("http is never classified") << "http://signin.ebay.com/" << int(UrlClass::Other);
    QTest::newRow("other host") << "https://signin.example.com/" << int(UrlClass::Other);
    QTest::newRow("look-alike host") << "https://www.ebay.com.example.com/sh/landing" << int(UrlClass::Other);
}

void TestPageCheck::classify()
{
    QFETCH(QString, url);
    QFETCH(int, expected);
    QCOMPARE(int(classifyUrl(QUrl(url))), expected);
}

void TestPageCheck::signInStartIsAnAccountPage()
{
    QCOMPARE(classifyUrl(signInStartUrl()), UrlClass::AccountPage);
    QVERIFY(isAllowedMainFrameUrl(signInStartUrl(), false));
}

void TestPageCheck::allowedMainFrame_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("allowLocal");
    QTest::addColumn<bool>("expected");
    QTest::newRow("www") << "https://www.ebay.com/sch/i.html?_nkw=x" << false << true;
    QTest::newRow("bare domain") << "https://ebay.com/" << false << true;
    QTest::newRow("sign-in") << "https://signin.ebay.com/ws/x" << false << true;
    QTest::newRow("http") << "http://www.ebay.com/" << false << false;
    QTest::newRow("other site") << "https://www.example.com/" << false << false;
    QTest::newRow("look-alike") << "https://ebay.com.example.com/" << false << false;
    QTest::newRow("suffix without dot") << "https://notebay.com/" << false << false;
    QTest::newRow("file, production") << "file:///tmp/page.html" << false << false;
    QTest::newRow("file, test build") << "file:///tmp/page.html" << true << true;
    QTest::newRow("loopback, production") << "http://127.0.0.1:8080/x" << false << false;
    QTest::newRow("loopback, test build") << "http://127.0.0.1:8080/x" << true << true;
    QTest::newRow("tls loopback, test build") << "https://127.0.0.1:8443/x" << true << true;
    QTest::newRow("tls loopback, production") << "https://127.0.0.1:8443/x" << false << false;
    QTest::newRow("other http, test build") << "http://192.0.2.1/x" << true << false;
    QTest::newRow("data") << "data:text/html,x" << true << false;
}

void TestPageCheck::allowedMainFrame()
{
    QFETCH(QString, url);
    QFETCH(bool, allowLocal);
    QFETCH(bool, expected);
    QCOMPARE(isAllowedMainFrameUrl(QUrl(url), allowLocal), expected);
}

void TestPageCheck::itemId_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("expected");
    QTest::newRow("plain") << "https://www.ebay.com/itm/100000000001" << "100000000001";
    QTest::newRow("query") << "https://www.ebay.com/itm/100000000001?hash=x" << "100000000001";
    QTest::newRow("slug after") << "https://www.ebay.com/itm/100000000001/extra" << "100000000001";
    QTest::newRow("not digits") << "https://www.ebay.com/itm/abc" << "";
    QTest::newRow("digits then text") << "https://www.ebay.com/itm/123abc" << "";
    QTest::newRow("search") << "https://www.ebay.com/sch/i.html" << "";
    QTest::newRow("other host") << "https://www.example.com/itm/100000000001" << "";
    QTest::newRow("http") << "http://www.ebay.com/itm/100000000001" << "";
}

void TestPageCheck::itemId()
{
    QFETCH(QString, url);
    QFETCH(QString, expected);
    QCOMPARE(itemIdFromUrl(QUrl(url)), expected);
}

void TestPageCheck::logUrlDropsQueryAndCredentials()
{
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://user:pw@signin.ebay.com/ws/x?token=secret#frag"))),
             QStringLiteral("https://signin.ebay.com/ws/x"));
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://www.ebay.com/sh/landing"))),
             QStringLiteral("https://www.ebay.com/sh/landing"));
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://www.ebay.com/itm/100000000001?hash=x"))),
             QStringLiteral("https://www.ebay.com/itm/100000000001"));
    // Paths of other pages may hold a name and are not logged.
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://www.ebay.com/usr/someone"))),
             QStringLiteral("https://www.ebay.com/..."));
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://www.ebay.com/str/somestore?x=1"))),
             QStringLiteral("https://www.ebay.com/..."));
    QCOMPARE(urlForLog(QUrl(QStringLiteral("https://www.ebay.com/"))), QStringLiteral("https://www.ebay.com/"));
}

void TestPageCheck::pageKindNames()
{
    for (PageKind kind : {PageKind::ActiveSearch, PageKind::SoldSearch, PageKind::Item, PageKind::Research})
        QCOMPARE(pageKindFromName(pageKindName(kind)), std::optional<PageKind>(kind));
    QCOMPARE(pageKindName(PageKind::SoldSearch), QStringLiteral("soldSearch"));
    QVERIFY(!pageKindFromName(QStringLiteral("unknown")).has_value());
}

QTEST_GUILESS_MAIN(TestPageCheck)
#include "tst_pagecheck.moc"
