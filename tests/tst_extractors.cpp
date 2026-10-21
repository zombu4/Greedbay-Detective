// Extractor tests through a real installed browser: each synthetic fixture
// (tests/fixtures/) is served by a local test server under the path and query
// of the eBay URL it stands for, fetched by BrowserController (DevTools
// protocol), and the extractor's result is checked field by field. No network
// is used. On a computer without a supported browser every test is skipped,
// and the output says so.
//
// Also checked: no browser process exists before the session, the browser
// opens no automation marker (navigator.webdriver is false), and after stop()
// every process the browser started has ended.
#include "browser/BrowserCatalog.h"
#include "browser/BrowserController.h"
#include "core/PageBudget.h"
#include "core/WorkFolder.h"
#include "support/ProcessList.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <optional>

using app::browser::BrowserController;
using app::ebay::PageKind;

namespace {

const char kActiveUrl[] = "https://www.ebay.com/sch/i.html?_nkw=test+meter&_ipg=240";
const char kSoldUrl[] = "https://www.ebay.com/sch/i.html?_nkw=test+meter&LH_Sold=1&LH_Complete=1&_ipg=240&_pgn=3";
const char kItemUrl[] = "https://www.ebay.com/itm/100000000010";
const char kItemDomUrl[] = "https://www.ebay.com/itm/100000000030?hash=x";
const char kItemInlineUrl[] = "https://www.ebay.com/itm/100000000031";
const char kResearchUrl[] = "https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=test+meter"
                            "&dayRange=1095&tabName=SOLD&limit=50&offset=0";

QSet<QString> names(const QJsonValue &array)
{
    QSet<QString> result;
    for (const QJsonValue &value : array.toArray())
        result.insert(value.toString());
    return result;
}

QSet<QString> set(std::initializer_list<const char *> values)
{
    QSet<QString> result;
    for (const char *value : values)
        result.insert(QString::fromLatin1(value));
    return result;
}

} // namespace

class TestExtractors : public QObject {
    Q_OBJECT

private:
    // Serves `fixture` under the path and query of `url` on the local test
    // server, fetches it through the browser as `kind` and returns the
    // result as {"page":"expected","data":{...}} or {"page":<kind>,"reason":...};
    // std::nullopt (with *error) when the fetch failed or did not finish.
    std::optional<QJsonObject> extract(const char *fixture, const char *url, PageKind kind, QString *error);
    void onConnection();

    QTemporaryDir m_temp;
    std::optional<app::WorkFolder> m_folder;
    BrowserController *m_controller = nullptr;
    QTcpServer m_server;
    QByteArray m_fixture;
    // The HTTP status line the test server answers with.
    QByteArray m_statusLine = "HTTP/1.1 200 OK";
    QString m_lastUserMessage;
    // How long the test, playing the user, waits before pressing Continue.
    int m_continueDelayMs = 0;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void activeSearch();
    void soldSearch();
    void itemFromJsonLd();
    void itemFromDom();
    void itemShippingBlockOnOneLine();
    void itemAuction();
    void research();
    void stopsAtFewerWordsDivider();
    void zeroResultsIsAnEmptyResult();
    void emptyResearchIsAnEmptyResult();
    void shipToPromptIsReported();
    void hiddenShipToDialogDoesNotBlock();
    void errorPage_data();
    void errorPage();
    void unexpectedPage_data();
    void unexpectedPage();
    void noAutomationMarker();
    void pageReadAgainAfterContinue();
    void failedLoadIsNotReadAfterContinue();
    void feedbackPage();
    void purchasesPage();
    void dailyCapStopsLoads();
    void browserClosesCleanly();
};

void TestExtractors::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n"))
                return;
            const QList<QByteArray> line = request.left(request.indexOf("\r\n")).split(' ');
            const QByteArray path = line.size() >= 2 ? line.at(1) : QByteArray();
            QByteArray response;
            if (path.startsWith("/favicon")) {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else {
                response = m_statusLine + "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
                           "Content-Length: " + QByteArray::number(m_fixture.size()) + "\r\nConnection: close\r\n\r\n"
                           + m_fixture;
            }
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
}

std::optional<QJsonObject> TestExtractors::extract(const char *fixture, const char *url, PageKind kind,
                                                   QString *error)
{
    QFile file(QStringLiteral(":/fixtures/") + QString::fromLatin1(fixture));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("fixture not found");
        return std::nullopt;
    }
    m_fixture = file.readAll();
    const QUrl ebayUrl(QString::fromLatin1(url));
    QUrl local(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    local.setPath(ebayUrl.path());
    local.setQuery(ebayUrl.query());
    // The feedback page carries the kind to list in the fragment.
    local.setFragment(ebayUrl.fragment());
    QSignalSpy done(m_controller, &BrowserController::done);
    m_controller->fetch(local, kind);
    if (!done.wait(60000) && done.isEmpty()) {
        *error = QStringLiteral("the fetch did not finish");
        return std::nullopt;
    }
    const QJsonObject reply = done.at(0).at(0).toJsonObject();
    const QString status = reply.value("status").toString();
    if (status == QLatin1String("ok"))
        return QJsonObject{{"page", "expected"}, {"data", reply.value("result")}};
    if (status == QLatin1String("unexpected_page"))
        return QJsonObject{{"page", reply.value("page")}, {"reason", reply.value("reason")}};
    *error = QStringLiteral("fetch failed: %1").arg(reply.value("error").toString());
    return std::nullopt;
}

void TestExtractors::initTestCase()
{
    const QList<app::browser::InstalledBrowser> browsers = app::browser::BrowserCatalog::installed();
    if (browsers.isEmpty())
        QSKIP("no supported browser (Brave, Google Chrome, Microsoft Edge, Chromium) is installed here; "
              "the browser-driven extractor tests are skipped");
    QVERIFY(m_temp.isValid());
    QString error;
    m_folder = app::WorkFolder::open(m_temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(m_folder, qPrintable(error));
    connect(&m_server, &QTcpServer::newConnection, this, &TestExtractors::onConnection);
    QVERIFY(m_server.listen(QHostAddress::LocalHost));

    // No browser process before the session.
    QVERIFY(descendants(QCoreApplication::applicationPid()).isEmpty());

    app::browser::BrowserControllerOptions options;
    options.pageDelayMinMs = 0; // no pacing against the local test server
    options.pageDelayMaxMs = 0;
    options.allowLocalTestPages = true;
    m_controller = new BrowserController(*m_folder, browsers.first(), options, this);
    qInfo().noquote() << "browser:" << browsers.first().name << browsers.first().executable;
    connect(m_controller, &BrowserController::needsUser, this, [this](const QString &message) {
        // The test plays the user: it looks at nothing and presses Continue.
        m_lastUserMessage = message;
        QTimer::singleShot(m_continueDelayMs, m_controller, &BrowserController::continueAfterUser);
    });
    QSignalSpy ready(m_controller, &BrowserController::ready);
    QSignalSpy failed(m_controller, &BrowserController::failed);
    QVERIFY2(m_controller->start(&error), qPrintable(error));
    // Past the controller's own start timeout, so a failed start reports its
    // reason here.
    QVERIFY2(ready.wait(BrowserController::kStartTimeoutMs + 5000) || !ready.isEmpty(),
             failed.isEmpty() ? "the browser did not get ready" : qPrintable(failed.at(0).at(0).toString()));
}

void TestExtractors::cleanupTestCase()
{
    // browserClosesCleanly() normally stopped the browser already.
    delete m_controller;
    m_controller = nullptr;
}

void TestExtractors::activeSearch()
{
    QString error;
    const std::optional<QJsonObject> result = extract("active_search.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("kind").toString(), QStringLiteral("activeSearch"));
    QCOMPARE(data.value("totalResults").toInt(), 612);
    QCOMPARE(data.value("totalIsLowerBound").toBool(), false);
    QCOMPARE(data.value("page").toInt(), 1);
    QCOMPARE(data.value("perPage").toInt(), 240);
    QCOMPARE(data.value("hasMorePages").toBool(), true);
    // The dummy card is found by its /itm/123456 link; its data-listingid
    // is another number, as on the real page.
    QCOMPARE(data.value("skippedDummyCards").toInt(), 1);
    // "Top Rated Plus" and "Watch this space" match no pattern.
    QCOMPARE(data.value("unrecognizedRows").toInt(), 2);
    QCOMPARE(data.value("unrecognizedSamples").toArray(), QJsonArray({"Top Rated Plus", "Watch this space"}));

    const QJsonArray items = data.value("items").toArray();
    QCOMPARE(items.size(), 4);
    for (const QJsonValue &item : items)
        QVERIFY(item.toObject().value("itemId").toString() != QLatin1String("123456"));

    // A fixed-price card with the earlier price struck through, a seller row
    // in two spans and the optional counters.
    const QJsonObject a = items.at(0).toObject();
    QCOMPARE(a.value("itemId").toString(), QStringLiteral("100000000001"));
    QCOMPARE(a.value("title").toString(), QStringLiteral("Acme Model 7 Digital Multimeter"));
    QCOMPARE(a.value("condition").toString(), QStringLiteral("Pre-Owned"));
    QCOMPARE(a.value("price").toObject().value("minCents").toInt(), 27400);
    QCOMPARE(a.value("price").toObject().value("maxCents").toInt(), 27400);
    QCOMPARE(a.value("price").toObject().value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(a.value("priceText").toString(), QStringLiteral("$274.00"));
    QCOMPARE(a.value("originalPriceCents").toInt(), 30000);
    QCOMPARE(a.value("shippingCents").toInt(), 1125);
    QCOMPARE(a.value("format").toString(), QStringLiteral("fixed_price"));
    QVERIFY(a.value("bids").isNull());
    QCOMPARE(a.value("buyItNow").toBool(), false);
    QCOMPARE(a.value("bestOfferOffered").toBool(), true);
    QCOMPARE(a.value("bestOfferAccepted").toBool(), false);
    QCOMPARE(a.value("freeReturns").toBool(), true);
    QCOMPARE(a.value("location").toString(), QStringLiteral("United States"));
    QCOMPARE(a.value("listedAt").toString(), QStringLiteral("Mar-02 09:15"));
    QCOMPARE(a.value("watchers").toInt(), 12);
    QCOMPARE(a.value("importFees").toString(), QStringLiteral("included"));
    QCOMPARE(a.value("seller").toObject().value("name").toString(), QStringLiteral("examplesellerone"));
    QCOMPARE(a.value("seller").toObject().value("positivePercent").toDouble(), 97.4);
    QCOMPARE(a.value("seller").toObject().value("feedbackCount").toInt(), 215);
    QCOMPARE(a.value("seller").toObject().value("feedbackText").toString(), QStringLiteral("215"));
    // Optional fields that are empty (bids, coupon, ...) are not "missing".
    QCOMPARE(names(a.value("missing")), QSet<QString>());
    QVERIFY(!a.contains("soldDate"));
    QVERIFY(!a.contains("priceStatus"));
    QVERIFY(!a.contains("offerRowUnrecognized"));

    // An auction: bids and time left share one row; an abbreviated feedback
    // count gives no exact number.
    const QJsonObject b = items.at(1).toObject();
    QCOMPARE(b.value("itemId").toString(), QStringLiteral("100000000002"));
    QCOMPARE(b.value("title").toString(), QStringLiteral("Acme Model 7 Meter with Leads"));
    QCOMPARE(b.value("condition").toString(), QStringLiteral("Brand New"));
    QCOMPARE(b.value("price").toObject().value("minCents").toInt(), 15000);
    QCOMPARE(b.value("shippingCents").toInt(), 0);
    QCOMPARE(b.value("format").toString(), QStringLiteral("auction"));
    QCOMPARE(b.value("bids").toInt(), 12);
    QCOMPARE(b.value("timeLeft").toString(), QStringLiteral("2d 5h left"));
    QCOMPARE(b.value("auctionEnd").toString(), QStringLiteral("Fri, 07:30 PM"));
    QCOMPARE(b.value("location").toString(), QStringLiteral("Canada"));
    QCOMPARE(b.value("seller").toObject().value("name").toString(), QStringLiteral("examplesellertwo"));
    QCOMPARE(b.value("seller").toObject().value("positivePercent").toDouble(), 100.0);
    QVERIFY(b.value("seller").toObject().value("feedbackCount").isNull());
    QCOMPARE(b.value("seller").toObject().value("feedbackText").toString(), QStringLiteral("1.2K"));
    QCOMPARE(names(b.value("missing")), set({"seller.feedbackCount"}));

    // A price range (listing with variations) and the remaining row kinds.
    // The title starts with "New Listings", which is not the marker.
    const QJsonObject c = items.at(2).toObject();
    QCOMPARE(c.value("itemId").toString(), QStringLiteral("100000000003"));
    QCOMPARE(c.value("title").toString(), QStringLiteral("New Listings Display Rack"));
    QCOMPARE(c.value("price").toObject().value("minCents").toInt(), 2000);
    QCOMPARE(c.value("price").toObject().value("maxCents").toInt(), 3500);
    QCOMPARE(c.value("buyItNow").toBool(), true);
    QCOMPARE(c.value("format").toString(), QStringLiteral("fixed_price"));
    QCOMPARE(c.value("soldCount").toObject().value("count").toInt(), 355);
    QCOMPARE(c.value("soldCount").toObject().value("atLeast").toBool(), false);
    QCOMPARE(c.value("stockNote").toString(), QStringLiteral("Last one"));
    QCOMPARE(c.value("couponPriceCents").toInt(), 1800);
    QCOMPARE(c.value("multiBuyText").toString(), QStringLiteral("Save up to 10% when you buy more"));
    QCOMPARE(c.value("importFees").toString(), QStringLiteral("due_before_delivery"));
    QCOMPARE(names(c.value("missing")), set({"condition", "shippingCents", "location", "seller"}));

    // No data-listingid: the item number comes from the link; the price text
    // is not a price.
    const QJsonObject d = items.at(3).toObject();
    QCOMPARE(d.value("itemId").toString(), QStringLiteral("100000000004"));
    QVERIFY(d.value("price").isNull());
    QCOMPARE(d.value("priceText").toString(), QStringLiteral("see price"));
    QCOMPARE(d.value("shippingCents").toInt(), 750);
    QCOMPARE(d.value("stockNote").toString(), QStringLiteral("Almost gone"));
    QCOMPARE(d.value("soldCount").toObject().value("count").toInt(), 39);
    QCOMPARE(d.value("soldCount").toObject().value("atLeast").toBool(), true);
    QVERIFY(d.value("seller").isNull());
    QCOMPARE(names(d.value("missing")), set({"price", "format", "location", "seller"}));
}

void TestExtractors::soldSearch()
{
    QString error;
    const std::optional<QJsonObject> result = extract("sold_search.html", kSoldUrl, PageKind::SoldSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("kind").toString(), QStringLiteral("soldSearch"));
    QCOMPARE(data.value("totalResults").toInt(), 487);
    QCOMPARE(data.value("page").toInt(), 3);
    QCOMPARE(data.value("hasMorePages").toBool(), false);
    QCOMPARE(data.value("skippedDummyCards").toInt(), 1);
    QCOMPARE(data.value("unrecognizedRows").toInt(), 0);
    const QJsonArray items = data.value("items").toArray();
    QCOMPARE(items.size(), 3);

    // Best offer accepted: the shown price is the asking price -> unconfirmed.
    const QJsonObject a = items.at(0).toObject();
    QCOMPARE(a.value("soldDate").toString(), QStringLiteral("2026-03-14"));
    QCOMPARE(a.value("price").toObject().value("minCents").toInt(), 9500);
    QCOMPARE(a.value("bestOfferAccepted").toBool(), true);
    QCOMPARE(a.value("priceStatus").toString(), QStringLiteral("unconfirmed"));
    QCOMPARE(a.value("format").toString(), QStringLiteral("fixed_price"));
    QCOMPARE(a.value("shippingCents").toInt(), 845);
    QCOMPARE(a.value("seller").toObject().value("positivePercent").toDouble(), 99.0);
    QCOMPARE(names(a.value("missing")), QSet<QString>());

    const QJsonObject b = items.at(1).toObject();
    QCOMPARE(b.value("soldDate").toString(), QStringLiteral("2026-03-09"));
    QCOMPARE(b.value("bids").toInt(), 23);
    QCOMPARE(b.value("format").toString(), QStringLiteral("auction"));
    QCOMPARE(b.value("priceStatus").toString(), QStringLiteral("shown"));
    QCOMPARE(b.value("price").toObject().value("minCents").toInt(), 12150);

    // "or Best Offer" without "Best offer accepted": sold at the shown price.
    // "Feb 30" is not a date, so the sold date is missing.
    const QJsonObject c = items.at(2).toObject();
    QVERIFY(c.value("soldDate").isNull());
    QCOMPARE(c.value("price").toObject().value("minCents").toInt(), 104000);
    QCOMPARE(c.value("bestOfferOffered").toBool(), true);
    QCOMPARE(c.value("priceStatus").toString(), QStringLiteral("shown"));
    QVERIFY(names(c.value("missing")).contains(QStringLiteral("soldDate")));
}

void TestExtractors::itemFromJsonLd()
{
    QString error;
    const std::optional<QJsonObject> result = extract("item_jsonld.html", kItemUrl, PageKind::Item, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    // The local test URL is not an eBay URL, so the item number is missing
    // (itemIdFromUrl accepts ebay.com only; tested in tst_pagecheck).
    QVERIFY(data.value("itemId").isNull());
    QCOMPARE(data.value("title").toString(), QStringLiteral("Acme Model 7 Digital Multimeter & Case 'Pro' &lt; (JSON-LD title)"));
    QCOMPARE(data.value("titleSource").toString(), QStringLiteral("json-ld"));
    QCOMPARE(data.value("imageUrl").toString(), QStringLiteral("https://images.example.com/acme-model-7-front.jpg"));
    QCOMPARE(data.value("priceCents").toInt(), 24995);
    QCOMPARE(data.value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(data.value("priceSource").toString(), QStringLiteral("json-ld"));
    QCOMPARE(data.value("condition").toString(), QStringLiteral("UsedCondition"));
    QCOMPARE(data.value("conditionSource").toString(), QStringLiteral("json-ld"));
    // What eBay shows, not the schema.org code.
    QCOMPARE(data.value("conditionText").toString(), QStringLiteral("Used"));
    QCOMPARE(data.value("availability").toString(), QStringLiteral("InStock"));
    QCOMPARE(data.value("listingStartedAt").toString(), QStringLiteral("2026-10-01T12:00:00.000Z"));
    const QJsonArray options = data.value("shippingOptions").toArray();
    QCOMPARE(options.size(), 1);
    QCOMPARE(options.at(0).toObject().value("amountCents").toInt(), 1250);
    QCOMPARE(options.at(0).toObject().value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(options.at(0).toObject().value("country").toString(), QStringLiteral("US"));
    QCOMPARE(data.value("shippingCents").toInt(), 1250);
    QCOMPARE(data.value("location").toString(), QStringLiteral("Springfield, Illinois, United States"));
    QCOMPARE(data.value("bestOfferOffered").toBool(), true);
    QCOMPARE(data.value("returnsText").toString(), QStringLiteral("30 days returns. Buyer pays for return shipping."));
    QCOMPARE(data.value("seller").toObject().value("name").toString(), QStringLiteral("examplestore"));
    // The user id from the feedback links (recon 2026-10-20).
    QCOMPARE(data.value("seller").toObject().value("id").toString(), QStringLiteral("examplestore"));
    QCOMPARE(data.value("seller").toObject().value("feedbackCount").toInt(), 4321);
    QCOMPARE(data.value("seller").toObject().value("positivePercent").toDouble(), 99.1);
    const QJsonArray specifics = data.value("specifics").toArray();
    QCOMPARE(specifics.size(), 2);
    QCOMPARE(specifics.at(0).toObject().value("label").toString(), QStringLiteral("Brand"));
    QCOMPARE(specifics.at(0).toObject().value("value").toString(), QStringLiteral("Acme"));
    QCOMPARE(specifics.at(1).toObject().value("label").toString(), QStringLiteral("MPN"));
    QCOMPARE(specifics.at(1).toObject().value("value").toString(), QStringLiteral("meter"));
    QCOMPARE(data.value("buyItNowOffered").toBool(), true);
    QCOMPARE(data.value("quantityAvailable").toInt(), 3);
    QCOMPARE(data.value("watchers").toInt(), 17);
    QCOMPARE(data.value("soldCount").toInt(), 3);
    QCOMPARE(data.value("offersInJsonLd").toInt(), 1);
    QCOMPARE(names(data.value("missing")), set({"itemId"}));

    // PRIVACY: the shipping block's text (with the viewer's ZIP, "00000"
    // here) never leaves the page.
    const QString json = QString::fromUtf8(QJsonDocument(*result).toJson(QJsonDocument::Compact));
    QVERIFY(!json.contains(QStringLiteral("to 00000")));
    QVERIFY(!json.contains(QStringLiteral("Delivery")));
    QVERIFY(!json.contains(QStringLiteral("Get it between")));
}

void TestExtractors::itemFromDom()
{
    QString error;
    const std::optional<QJsonObject> result = extract("item_dom.html", kItemDomUrl, PageKind::Item, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QVERIFY(data.value("itemId").isNull());
    QCOMPARE(data.value("title").toString(), QStringLiteral("Acme Bench Supply 30V 5A"));
    QCOMPARE(data.value("titleSource").toString(), QStringLiteral("dom"));
    QCOMPARE(data.value("priceCents").toInt(), 104900);
    QCOMPARE(data.value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(data.value("priceSource").toString(), QStringLiteral("dom"));
    QCOMPARE(data.value("condition").toString(), QStringLiteral("New"));
    QCOMPARE(data.value("conditionSource").toString(), QStringLiteral("dom"));
    QCOMPARE(data.value("shippingCents").toInt(), 0);
    QCOMPARE(data.value("location").toString(), QStringLiteral("Portland, Oregon, United States"));
    QCOMPARE(data.value("bestOfferOffered").toBool(), false);
    QCOMPARE(data.value("shippingOptions").toArray().size(), 0);
    QCOMPARE(data.value("specifics").toArray().size(), 0);
    QCOMPARE(data.value("offersInJsonLd").toInt(), 0);
    QCOMPARE(data.value("buyItNowOffered").toBool(), false);
    QVERIFY(data.value("quantityAvailable").isNull());
    // watchers, soldCount and quantityAvailable are optional: null, not missing.
    QVERIFY(data.value("watchers").isNull());
    QCOMPARE(names(data.value("missing")),
             set({"itemId", "imageUrl", "availability", "listingStartedAt", "returnsText", "seller"}));
}

// An auction item page (structure seen 2026-10-19): the auction, its bids
// and the end texts; no Buy It Now. A fixed-price page has none of them.
void TestExtractors::itemAuction()
{
    QString error;
    const std::optional<QJsonObject> result = extract("item_auction.html", kItemDomUrl, PageKind::Item, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("auction").toBool(), true);
    QCOMPARE(data.value("bids").toInt(), 3);
    QCOMPARE(data.value("endsIn").toString(), QStringLiteral("3d 22h"));
    QCOMPARE(data.value("endsAt").toString(), QStringLiteral("Friday, 06:28 PM"));
    QCOMPARE(data.value("buyItNowOffered").toBool(), false);
    QCOMPARE(data.value("priceCents").toInt(), 12500);

    const std::optional<QJsonObject> fixed = extract("item_jsonld.html", kItemUrl, PageKind::Item, &error);
    QVERIFY2(fixed, qPrintable(error));
    const QJsonObject fixedData = fixed->value("data").toObject();
    QCOMPARE(fixedData.value("auction").toBool(), false);
    QVERIFY(fixedData.value("bids").isNull());
    QVERIFY(fixedData.value("endsIn").isNull());
    QCOMPARE(fixedData.value("buyItNowOffered").toBool(), true);
}

void TestExtractors::itemShippingBlockOnOneLine()
{
    QString error;
    const std::optional<QJsonObject> result = extract("item_inline.html", kItemInlineUrl, PageKind::Item, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("priceCents").toInt(), 6400);
    // One rendered line: no line starts with the cost, and the text after
    // "Located in:" holds digits, so neither value is taken.
    QVERIFY(data.value("shippingCents").isNull());
    QVERIFY(data.value("location").isNull());
    // The whole first element of the condition box, not its first word.
    QCOMPARE(data.value("condition").toString(), QStringLiteral("New other (see details)"));
    QCOMPARE(data.value("conditionText").toString(), QStringLiteral("New other (see details)"));
    QCOMPARE(data.value("conditionSource").toString(), QStringLiteral("dom"));
    // One "watchers" counter on the page; two "sold" counters of other
    // listings, so the item's own count is unknown.
    QCOMPARE(data.value("watchers").toInt(), 9);
    QVERIFY(data.value("soldCount").isNull());
    QVERIFY(names(data.value("missing")).contains(QStringLiteral("shippingCents")));
    QVERIFY(names(data.value("missing")).contains(QStringLiteral("location")));
    QVERIFY(!names(data.value("missing")).contains(QStringLiteral("soldCount")));

    const QString json = QString::fromUtf8(QJsonDocument(*result).toJson(QJsonDocument::Compact));
    // (The item number contains "00000" too, so the ZIP is searched with
    // the word before it.)
    QVERIFY(!json.contains(QStringLiteral("to 00000")));
    QVERIFY(!json.contains(QStringLiteral("Delivery")));
    QVERIFY(!json.contains(QStringLiteral("Springfield")));
}

void TestExtractors::research()
{
    QString error;
    const std::optional<QJsonObject> result = extract("research.html", kResearchUrl, PageKind::Research, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("offset").toInt(), 0);
    QCOMPARE(data.value("limit").toInt(), 50);
    QCOMPARE(data.value("dayRange").toInt(), 1095);
    QCOMPARE(data.value("rowCount").toInt(), 4);
    QCOMPARE(data.value("morePossible").toBool(), false);

    // Read from the section.aggregate-metric blocks only: the table header
    // and a chart title repeat "Avg sold price" (the chart with another
    // amount), one section has its value after the header, the price range
    // uses an EN DASH, and "Sell-through" shows "-".
    const QJsonObject summary = data.value("summary").toObject();
    QCOMPARE(summary.value("avgSoldPriceCents").toInt(), 4170);
    QCOMPARE(summary.value("soldPriceRange").toObject().value("minCents").toInt(), 1200);
    QCOMPARE(summary.value("soldPriceRange").toObject().value("maxCents").toInt(), 8050);
    QCOMPARE(summary.value("avgShippingCents").toInt(), 585);
    QCOMPARE(summary.value("freeShippingPercent").toDouble(), 45.0);
    QVERIFY(summary.value("sellThroughPercent").isNull());
    QCOMPARE(summary.value("totalSellers").toInt(), 1021);
    QCOMPARE(names(summary.value("missing")), set({"sellThroughPercent"}));

    const QJsonArray rows = data.value("rows").toArray();
    QCOMPARE(rows.size(), 4);
    const QJsonObject r1 = rows.at(0).toObject();
    QCOMPARE(r1.value("itemId").toString(), QStringLiteral("100000000020"));
    QCOMPARE(r1.value("title").toString(), QStringLiteral("Acme Model 7 Multimeter"));
    QCOMPARE(r1.value("avgSoldPriceCents").toInt(), 4170);
    QCOMPARE(r1.value("formatText").toString(), QStringLiteral("Fixed price"));
    QCOMPARE(r1.value("avgShippingCents").toInt(), 585);
    QCOMPARE(r1.value("freeShippingPercent").toDouble(), 0.0);
    QCOMPARE(r1.value("totalSold").toInt(), 4);
    QCOMPARE(r1.value("totalSalesCents").toInt(), 16680);
    QCOMPARE(r1.value("bids").toInt(), 0); // shown as an EN DASH
    QCOMPARE(r1.value("lastSoldDate").toString(), QStringLiteral("2026-03-03"));
    QCOMPARE(names(r1.value("missing")), QSet<QString>());

    // A row with only the item number in place of a title.
    const QJsonObject r2 = rows.at(1).toObject();
    QCOMPARE(r2.value("itemId").toString(), QStringLiteral("100000000021"));
    QVERIFY(r2.value("title").isNull());
    QCOMPARE(r2.value("avgSoldPriceCents").toInt(), 3320);
    QCOMPARE(r2.value("formatText").toString(), QStringLiteral("Auction"));
    QCOMPARE(r2.value("avgShippingCents").toInt(), 0);
    QCOMPARE(r2.value("freeShippingPercent").toDouble(), 100.0);
    QCOMPARE(r2.value("bids").toInt(), 7);
    QCOMPARE(r2.value("lastSoldDate").toString(), QStringLiteral("2026-09-30"));
    QCOMPARE(names(r2.value("missing")), set({"title"}));

    const QJsonObject r3 = rows.at(2).toObject();
    QCOMPARE(r3.value("itemId").toString(), QStringLiteral("100000000022"));
    QCOMPARE(r3.value("avgSoldPriceCents").toInt(), 120000);
    QCOMPARE(r3.value("totalSold").toInt(), 2);
    QCOMPARE(names(r3.value("missing")), set({"formatText", "avgShippingCents", "freeShippingPercent",
                                               "totalSalesCents", "bids", "lastSoldDate"}));

    // An old sale: title and item number from the span, no link.
    const QJsonObject r4 = rows.at(3).toObject();
    QCOMPARE(r4.value("itemId").toString(), QStringLiteral("100000000023"));
    QCOMPARE(r4.value("title").toString(), QStringLiteral("Acme Model 7 Multimeter WORKING"));
    QCOMPARE(r4.value("avgSoldPriceCents").toInt(), 3650);
    QCOMPARE(r4.value("totalSold").toInt(), 2);
    QCOMPARE(r4.value("lastSoldDate").toString(), QStringLiteral("2025-10-15"));
}

void TestExtractors::stopsAtFewerWordsDivider()
{
    QString error;
    const std::optional<QJsonObject> result = extract("fewer_words.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("totalResults").toInt(), 3);
    QCOMPARE(data.value("fewerWordsDivider").toBool(), true);
    // The 3 cards before "Results matching fewer words" (the live-events
    // block between them is no card); the 2 after it are not results.
    QCOMPARE(data.value("paddingCards").toInt(), 2);
    const QJsonArray items = data.value("items").toArray();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(2).toObject().value("itemId").toString(), QStringLiteral("100000000062"));
    QCOMPARE(data.value("hasMorePages").toBool(true), false);
}

void TestExtractors::zeroResultsIsAnEmptyResult()
{
    QString error;
    const std::optional<QJsonObject> result = extract("zero_results.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("totalResults").toInt(), 0);
    // "0 results" with cards below: every card is padding.
    QCOMPARE(data.value("items").toArray().size(), 0);
    QCOMPARE(data.value("paddingCards").toInt(), 2);
    QCOMPARE(data.value("hasMorePages").toBool(true), false);
}

void TestExtractors::emptyResearchIsAnEmptyResult()
{
    QString error;
    const std::optional<QJsonObject> result = extract("research_empty.html", kResearchUrl, PageKind::Research, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("rowCount").toInt(), 0);
    QCOMPARE(data.value("emptyResult").toBool(), true);
    QCOMPARE(data.value("morePossible").toBool(true), false);
}

void TestExtractors::shipToPromptIsReported()
{
    // No results and eBay's ship-to prompt shown: reported as such, never
    // filled in (the user answers it in the window).
    QString error;
    const std::optional<QJsonObject> result = extract("ship_to.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("ship_to"));
    QVERIFY2(m_lastUserMessage.contains(QStringLiteral("eBay asks where you ship to")), qPrintable(m_lastUserMessage));
}

void TestExtractors::hiddenShipToDialogDoesNotBlock()
{
    QString error;
    const std::optional<QJsonObject> result = extract("ship_to_hidden.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    QCOMPARE(result->value("data").toObject().value("items").toArray().size(), 1);
}

void TestExtractors::errorPage_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("url");
    QTest::newRow("activeSearch") << int(PageKind::ActiveSearch) << QString::fromLatin1(kActiveUrl);
    QTest::newRow("soldSearch") << int(PageKind::SoldSearch) << QString::fromLatin1(kSoldUrl);
    QTest::newRow("item") << int(PageKind::Item) << QString::fromLatin1(kItemUrl);
    QTest::newRow("research") << int(PageKind::Research) << QString::fromLatin1(kResearchUrl);
}

void TestExtractors::errorPage()
{
    QFETCH(int, kind);
    QFETCH(QString, url);
    QString error;
    const QByteArray urlBytes = url.toLatin1();
    const std::optional<QJsonObject> result =
        extract("error_page.html", urlBytes.constData(), static_cast<PageKind>(kind), &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("error_page"));
    QVERIFY(!result->contains("data"));
}

void TestExtractors::unexpectedPage_data()
{
    // Each case waits the 10 s settle time; two kinds are enough to show it.
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("url");
    QTest::newRow("activeSearch") << int(PageKind::ActiveSearch) << QString::fromLatin1(kActiveUrl);
    QTest::newRow("item") << int(PageKind::Item) << QString::fromLatin1(kItemUrl);
}

void TestExtractors::unexpectedPage()
{
    QFETCH(int, kind);
    QFETCH(QString, url);
    QString error;
    const QByteArray urlBytes = url.toLatin1();
    const std::optional<QJsonObject> result =
        extract("check_page.html", urlBytes.constData(), static_cast<PageKind>(kind), &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("unexpected"));
    QVERIFY(!result->value("reason").toString().isEmpty());
    QVERIFY(!result->contains("data"));
}

void TestExtractors::noAutomationMarker()
{
    // Measured by the page itself (fixtures/webdriver_check.html): "1 results"
    // when navigator.webdriver is false. The browser runs as a normal browser.
    QString error;
    const std::optional<QJsonObject> result = extract("webdriver_check.html", kActiveUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    QCOMPARE(result->value("data").toObject().value("totalResults").toInt(), 1);
}

void TestExtractors::pageReadAgainAfterContinue()
{
    // The page is not a results page when it loads, so the app shows it and
    // waits; the "user" presses Continue 3 s later, by when the page has
    // become the requested results page: it is read then, without a reload.
    m_continueDelayMs = 3000;
    m_lastUserMessage.clear();
    QString error;
    const std::optional<QJsonObject> result = extract("late_page.html", kActiveUrl, PageKind::ActiveSearch, &error);
    m_continueDelayMs = 0;
    QVERIFY2(result, qPrintable(error));
    // The page was shown to the "user" first: it was read after Continue.
    QVERIFY(!m_lastUserMessage.isEmpty());
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("totalResults").toInt(), 7);
    QCOMPARE(data.value("items").toArray().size(), 1);
    QCOMPARE(data.value("items").toArray().at(0).toObject().value("itemId").toString(), QStringLiteral("100000000040"));
}

void TestExtractors::failedLoadIsNotReadAfterContinue()
{
    // A results page that came with HTTP 403: shown to the "user", and after
    // Continue still not read, although its content looks right.
    m_statusLine = "HTTP/1.1 403 Forbidden";
    QString error;
    const std::optional<QJsonObject> result = extract("active_search.html", kActiveUrl, PageKind::ActiveSearch, &error);
    m_statusLine = "HTTP/1.1 200 OK";
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("unexpected"));
    QVERIFY2(result->value("reason").toString().contains(QStringLiteral("HTTP status 403")),
             qPrintable(result->value("reason").toString()));
}

// The classic feedback page (recon 2026-10-20): the counts, then a click on
// the 12-month count of the wanted kind and the list as it is after the page
// replaced it in place. The buyer column is never read.
void TestExtractors::feedbackPage()
{
    QString error;
    const char *negative = "https://www.ebay.com/fdbk/feedback_profile/seller-a?filter=feedback_page%3ARECEIVED_AS_SELLER&sort=RELEVANCEV2#negative";
    std::optional<QJsonObject> result = extract("feedback.html", negative, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    QJsonObject data = result->value("data").toObject();
    QCOMPARE(data.value("sellerId").toString(), QStringLiteral("seller-a"));
    QCOMPARE(data.value("kind").toString(), QStringLiteral("negative"));
    QCOMPARE(data.value("score").toInt(), 3295);
    QCOMPARE(data.value("positive").toArray(), QJsonArray({185, 1023, 2071}));
    QCOMPARE(data.value("negative").toArray(), QJsonArray({0, 1, 2}));
    const QJsonArray reviews = data.value("reviews").toArray();
    QCOMPARE(reviews.size(), 2);
    const QJsonObject first = reviews.at(0).toObject();
    QCOMPARE(first.value("comment").toString(), QStringLiteral("Never arrived, no answer"));
    QCOMPARE(first.value("item").toString(), QStringLiteral("Acme Q9 Signal Tracer"));
    QCOMPARE(first.value("price").toString(), QStringLiteral("US $255.00"));
    QCOMPARE(first.value("when").toString(), QStringLiteral("Past 6 months"));
    QCOMPARE(first.value("verified").toBool(), true);
    QCOMPARE(reviews.at(1).toObject().value("verified").toBool(), false);
    const QByteArray text = QJsonDocument(data).toJson(QJsonDocument::Compact);
    QVERIFY(!text.contains("Buyer") && !text.contains("***"));

    // No neutral ratings: nothing is clicked, no reviews. (Another query than
    // the page before: a change of the fragment alone is no new page load.)
    const char *neutral = "https://www.ebay.com/fdbk/feedback_profile/seller-a?filter=feedback_page%3ARECEIVED_AS_SELLER&sort=TIME#neutral";
    result = extract("feedback.html", neutral, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    data = result->value("data").toObject();
    QCOMPARE(data.value("kind").toString(), QStringLiteral("neutral"));
    QVERIFY(data.value("reviews").toArray().isEmpty());

    // No kind in the address: not read.
    result = extract("feedback.html", "https://www.ebay.com/fdbk/feedback_profile/seller-a?sort=RELEVANCEV2", PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("unexpected"));

    // Only negatives already, but fewer than the count: clicked, and read once
    // the whole list is there.
    result = extract("feedback_partial.html", negative, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    QCOMPARE(result->value("data").toObject().value("reviews").toArray().size(), 2);
    QCOMPARE(result->value("data").toObject().value("reviews").toArray().at(1).toObject().value("comment").toString(),
             QStringLiteral("Broken on arrival"));
    // Exactly the wanted reviews already (a page read again after Continue):
    // read as it is, no click.
    const char *ready = "https://www.ebay.com/fdbk/feedback_profile/seller-a?filter=feedback_page%3ARECEIVED_AS_SELLER&sort=TIME#negative";
    result = extract("feedback_ready.html", ready, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonArray readyReviews = result->value("data").toObject().value("reviews").toArray();
    QCOMPARE(readyReviews.size(), 1);
    QCOMPARE(readyReviews.at(0).toObject().value("comment").toString(), QStringLiteral("Wrong part sent"));

    // More than 25: page 1 with a next page, then page 2 read on the page
    // (paced and counted like a load), then page 2 asked again (as after
    // Continue): no second click.
    const char *paged = "https://www.ebay.com/fdbk/feedback_profile/seller-a?filter=feedback_page%3ARECEIVED_AS_SELLER&sort=RELEVANCEV2&p=1#negative";
    result = extract("feedback_pages.html", paged, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject pageOne = result->value("data").toObject();
    QCOMPARE(pageOne.value("reviews").toArray().size(), 25);
    QCOMPARE(pageOne.value("page").toInt(), 1);
    QVERIFY(pageOne.value("hasNext").toBool());
    for (int round = 0; round < 2; ++round) {
        // Each read on the page counts as one page of the day's cap.
        const int pagesBefore = app::PageBudget(m_folder->pageCountPath(), 1000).usedToday();
        QSignalSpy again(m_controller, &BrowserController::done);
        m_controller->readOnPage(QStringLiteral("negative@2"));
        QVERIFY(again.wait(60000) || !again.isEmpty());
        const QJsonObject reply = again.at(0).at(0).toJsonObject();
        QCOMPARE(reply.value("status").toString(), QStringLiteral("ok"));
        const QJsonObject second = reply.value("result").toObject();
        QCOMPARE(second.value("page").toInt(), 2);
        QVERIFY(!second.value("hasNext").toBool());
        const QJsonArray rest = second.value("reviews").toArray();
        QCOMPARE(rest.size(), 5);
        QCOMPARE(rest.at(0).toObject().value("comment").toString(), QStringLiteral("Bad order 26"));
        QCOMPARE(app::PageBudget(m_folder->pageCountPath(), 1000).usedToday(), pagesBefore + 1);
    }
    // Page 3 of a 2-page list: said, not guessed.
    {
        QSignalSpy third(m_controller, &BrowserController::done);
        m_controller->readOnPage(QStringLiteral("negative@3"));
        if (!third.wait(60000) && third.isEmpty())
            QFAIL("no answer for page 3");
        QCOMPARE(third.at(0).at(0).toJsonObject().value("status").toString(), QStringLiteral("unexpected_page"));
    }

    // The list never switches: the extractor gives up and says so.
    result = extract("feedback_stuck.html", negative, PageKind::Feedback, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("unexpected"));
    QVERIFY(result->value("reason").toString().contains(QStringLiteral("did not switch")));
}

void TestExtractors::purchasesPage()
{
    // Page 1: 25 orders with the item number, the seller id and the month,
    // nothing else of the order; then page 2 read on the page (one click on
    // Next), twice (as after Continue: no second click).
    QString error;
    std::optional<QJsonObject> result
        = extract("purchases_pages.html", "https://www.ebay.com/mye/myebay/purchase", PageKind::Purchases, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("expected"));
    const QJsonObject pageOne = result->value("data").toObject();
    QCOMPARE(pageOne.value("page").toInt(), 1);
    QVERIFY(pageOne.value("hasNext").toBool());
    const QJsonArray orders = pageOne.value("orders").toArray();
    QCOMPARE(orders.size(), 25);
    const QJsonObject first = orders.at(0).toObject();
    QCOMPARE(first.value("itemId").toString(), QStringLiteral("100000000001"));
    QCOMPARE(first.value("sellerId").toString(), QStringLiteral("shop_1"));
    QCOMPARE(first.value("month").toString(), QStringLiteral("2026-02"));
    QCOMPARE(first.keys(), QStringList({QStringLiteral("itemId"), QStringLiteral("month"), QStringLiteral("sellerId")}));
    QVERIFY(orders.at(1).toObject().value("month").isNull()); // no date on the card: none made up
    QCOMPARE(orders.at(2).toObject().value("itemId").toString(), QStringLiteral("100000000003")); // title path
    const QByteArray text = QJsonDocument(pageOne).toJson(QJsonDocument::Compact);
    QVERIFY(!text.contains("US $") && !text.contains("00-0000"));

    for (int round = 0; round < 2; ++round) {
        QSignalSpy again(m_controller, &BrowserController::done);
        m_controller->readOnPage(QStringLiteral("@2"));
        QVERIFY(again.wait(60000) || !again.isEmpty());
        const QJsonObject reply = again.at(0).at(0).toJsonObject();
        QCOMPARE(reply.value("status").toString(), QStringLiteral("ok"));
        const QJsonObject second = reply.value("result").toObject();
        QCOMPARE(second.value("page").toInt(), 2);
        QVERIFY(!second.value("hasNext").toBool());
        const QJsonArray rest = second.value("orders").toArray();
        QCOMPARE(rest.size(), 3);
        QCOMPARE(rest.at(1).toObject().value("sellerId").toString(), QStringLiteral("wanted_seller"));
        QCOMPARE(rest.at(1).toObject().value("month").toString(), QStringLiteral("2026-04"));
    }
    {
        QSignalSpy wrong(m_controller, &BrowserController::done);
        m_controller->readOnPage(QStringLiteral("negative@2"));
        if (!wrong.wait(60000) && wrong.isEmpty())
            QFAIL("no answer for a feedback option on the purchases page");
        QCOMPARE(wrong.at(0).at(0).toJsonObject().value("status").toString(), QStringLiteral("unexpected_page"));
    }
}

void TestExtractors::dailyCapStopsLoads()
{
    // A second session with its own work folder and a cap of one page: the
    // first fetch loads, the second is refused before any load.
    QString error;
    const std::optional<app::WorkFolder> folder =
        app::WorkFolder::open(m_temp.path() + QStringLiteral("/capped"), TEST_APP_NAME, &error);
    QVERIFY2(folder, qPrintable(error));
    QFile file(QStringLiteral(":/fixtures/active_search.html"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    m_fixture = file.readAll();
    app::browser::BrowserControllerOptions options;
    options.pageDelayMinMs = 0;
    options.pageDelayMaxMs = 0;
    options.allowLocalTestPages = true;
    options.dailyPageCap = 1;
    BrowserController capped(*folder, app::browser::BrowserCatalog::installed().first(), options);
    QSignalSpy ready(&capped, &BrowserController::ready);
    QVERIFY2(capped.start(&error), qPrintable(error));
    QVERIFY(ready.wait(BrowserController::kStartTimeoutMs + 5000) || !ready.isEmpty());
    QUrl local(QStringLiteral("http://127.0.0.1:%1/sch/i.html?_nkw=test+meter&_ipg=240").arg(m_server.serverPort()));
    QSignalSpy done(&capped, &BrowserController::done);
    capped.fetch(local, PageKind::ActiveSearch);
    QVERIFY(done.wait(60000) || !done.isEmpty());
    QCOMPARE(done.at(0).at(0).toJsonObject().value("status").toString(), QStringLiteral("ok"));
    capped.fetch(local, PageKind::ActiveSearch);
    QVERIFY(QTest::qWaitFor([&done]() { return done.size() >= 2; }, 10000));
    const QJsonObject refused = done.at(1).at(0).toJsonObject();
    QCOMPARE(refused.value("status").toString(), QStringLiteral("limit_reached"));
    QVERIFY2(refused.value("error").toString().contains(QStringLiteral("limit 1")), qPrintable(refused.value("error").toString()));
    QFile count(folder->pageCountPath());
    QVERIFY(count.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(count.readAll()).object().value("pages").toInt(), 1);
    QSignalSpy finished(&capped, &BrowserController::finished);
    capped.stop();
    QVERIFY(finished.wait(BrowserController::kCloseTimeoutMs + 10000) || !finished.isEmpty());
    QCOMPARE(finished.at(0).at(0).toBool(), true);
}

void TestExtractors::browserClosesCleanly()
{
    QStringList seen;
    for (const ProcessInfo &process : descendants(QCoreApplication::applicationPid()))
        seen << QStringLiteral("%1 %2").arg(process.pid).arg(process.name);
    qInfo().noquote() << "browser processes during the session:" << seen.join(QStringLiteral(", "));
    QVERIFY(!seen.isEmpty());
    QSignalSpy finished(m_controller, &BrowserController::finished);
    m_controller->stop();
    QVERIFY(finished.wait(BrowserController::kCloseTimeoutMs + 10000) || !finished.isEmpty());
    QCOMPARE(finished.at(0).at(0).toBool(), true);
    QVERIFY(QTest::qWaitFor([]() { return descendants(QCoreApplication::applicationPid()).isEmpty(); }, 15000));
}

QTEST_GUILESS_MAIN(TestExtractors)
#include "tst_extractors.moc"
