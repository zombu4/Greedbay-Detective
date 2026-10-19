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
    QString m_lastUserMessage;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void activeSearch();
    void soldSearch();
    void itemFromJsonLd();
    void itemFromDom();
    void itemShippingBlockOnOneLine();
    void research();
    void errorPage_data();
    void errorPage();
    void unexpectedPage_data();
    void unexpectedPage();
    void noAutomationMarker();
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
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
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
        m_controller->continueAfterUser();
    });
    QSignalSpy ready(m_controller, &BrowserController::ready);
    QSignalSpy failed(m_controller, &BrowserController::failed);
    QVERIFY2(m_controller->start(&error), qPrintable(error));
    QVERIFY2(ready.wait(BrowserController::kStartTimeoutMs) || !ready.isEmpty(),
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
    QCOMPARE(data.value("skippedDummyCards").toInt(), 1);
    // "Top Rated Plus" and a seller row with "(2.1K)" match no pattern.
    QCOMPARE(data.value("unrecognizedRows").toInt(), 2);

    const QJsonArray items = data.value("items").toArray();
    QCOMPARE(items.size(), 4);
    for (const QJsonValue &item : items)
        QVERIFY(item.toObject().value("itemId").toString() != QLatin1String("123456"));

    const QJsonObject a = items.at(0).toObject();
    QCOMPARE(a.value("itemId").toString(), QStringLiteral("100000000001"));
    QCOMPARE(a.value("title").toString(), QStringLiteral("Acme Model 7 Digital Multimeter"));
    QCOMPARE(a.value("condition").toString(), QStringLiteral("Pre-Owned"));
    QCOMPARE(a.value("price").toObject().value("minCents").toInt(), 27400);
    QCOMPARE(a.value("price").toObject().value("maxCents").toInt(), 27400);
    QCOMPARE(a.value("price").toObject().value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(a.value("priceText").toString(), QStringLiteral("$274.00"));
    QCOMPARE(a.value("shippingCents").toInt(), 1125);
    QCOMPARE(a.value("format").toString(), QStringLiteral("fixed_price"));
    QVERIFY(a.value("bids").isNull());
    QCOMPARE(a.value("buyItNow").toBool(), false);
    QCOMPARE(a.value("bestOfferOffered").toBool(), true);
    QCOMPARE(a.value("bestOfferAccepted").toBool(), false);
    QCOMPARE(a.value("freeReturns").toBool(), true);
    QCOMPARE(a.value("location").toString(), QStringLiteral("United States"));
    QCOMPARE(a.value("listedAt").toString(), QStringLiteral("Mar-02 09:15"));
    QCOMPARE(a.value("seller").toObject().value("name").toString(), QStringLiteral("examplesellerone"));
    QCOMPARE(a.value("seller").toObject().value("positivePercent").toDouble(), 97.4);
    QCOMPARE(a.value("seller").toObject().value("feedbackCount").toInt(), 215);
    QCOMPARE(names(a.value("missing")), set({"bids"}));
    QVERIFY(!a.contains("soldDate"));
    QVERIFY(!a.contains("priceStatus"));

    const QJsonObject b = items.at(1).toObject();
    QCOMPARE(b.value("itemId").toString(), QStringLiteral("100000000002"));
    QCOMPARE(b.value("title").toString(), QStringLiteral("Acme Model 7 Meter with Leads"));
    QCOMPARE(b.value("condition").toString(), QStringLiteral("Brand New"));
    QCOMPARE(b.value("price").toObject().value("minCents").toInt(), 15000);
    QCOMPARE(b.value("shippingCents").toInt(), 0);
    QCOMPARE(b.value("format").toString(), QStringLiteral("auction"));
    QCOMPARE(b.value("bids").toInt(), 12);
    QCOMPARE(b.value("location").toString(), QStringLiteral("Canada"));
    QCOMPARE(b.value("seller").toObject().value("positivePercent").toDouble(), 100.0);
    QCOMPARE(b.value("seller").toObject().value("feedbackCount").toInt(), 1204);
    QCOMPARE(names(b.value("missing")), set({"listedAt"}));

    // A title that only starts with the words "New Listing" (no marker
    // element) is kept as it is.
    const QJsonObject c = items.at(2).toObject();
    QCOMPARE(c.value("itemId").toString(), QStringLiteral("100000000003"));
    QCOMPARE(c.value("title").toString(), QStringLiteral("New Listings Display Rack"));
    QCOMPARE(c.value("price").toObject().value("minCents").toInt(), 2000);
    QCOMPARE(c.value("price").toObject().value("maxCents").toInt(), 3500);
    QCOMPARE(c.value("buyItNow").toBool(), true);
    QCOMPARE(c.value("format").toString(), QStringLiteral("fixed_price"));
    QCOMPARE(names(c.value("missing")), set({"condition", "shippingCents", "bids", "location", "listedAt", "seller"}));

    // No data-listingid: the item number comes from the /itm/ link. The
    // price text matches no pattern, so the price is missing, not guessed.
    const QJsonObject d = items.at(3).toObject();
    QCOMPARE(d.value("itemId").toString(), QStringLiteral("100000000004"));
    QVERIFY(d.value("price").isNull());
    QCOMPARE(d.value("priceText").toString(), QStringLiteral("see price"));
    QCOMPARE(d.value("shippingCents").toInt(), 750);
    QVERIFY(d.value("seller").isNull());
    QCOMPARE(names(d.value("missing")), set({"price", "format", "bids", "location", "listedAt", "seller"}));
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
    QCOMPARE(names(a.value("missing")), set({"bids", "listedAt"}));

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
    QCOMPARE(data.value("title").toString(), QStringLiteral("Acme Model 7 Digital Multimeter (JSON-LD title)"));
    QCOMPARE(data.value("titleSource").toString(), QStringLiteral("json-ld"));
    QCOMPARE(data.value("priceCents").toInt(), 24995);
    QCOMPARE(data.value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(data.value("priceSource").toString(), QStringLiteral("json-ld"));
    QCOMPARE(data.value("condition").toString(), QStringLiteral("UsedCondition"));
    QCOMPARE(data.value("conditionSource").toString(), QStringLiteral("json-ld"));
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
    QCOMPARE(data.value("seller").toObject().value("feedbackCount").toInt(), 4321);
    QCOMPARE(data.value("seller").toObject().value("positivePercent").toDouble(), 99.1);
    const QJsonArray specifics = data.value("specifics").toArray();
    QCOMPARE(specifics.size(), 2);
    QCOMPARE(specifics.at(0).toObject().value("label").toString(), QStringLiteral("Brand"));
    QCOMPARE(specifics.at(0).toObject().value("value").toString(), QStringLiteral("Acme"));
    QCOMPARE(specifics.at(1).toObject().value("label").toString(), QStringLiteral("MPN"));
    QCOMPARE(specifics.at(1).toObject().value("value").toString(), QStringLiteral("meter"));
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
    QCOMPARE(names(data.value("missing")),
             set({"itemId", "availability", "listingStartedAt", "returnsText", "seller", "watchers", "soldCount"}));
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
    QCOMPARE(data.value("conditionSource").toString(), QStringLiteral("dom"));
    // One "watchers" counter on the page; two "sold" counters of other
    // listings, so the item's own count is unknown.
    QCOMPARE(data.value("watchers").toInt(), 9);
    QVERIFY(data.value("soldCount").isNull());
    QVERIFY(names(data.value("missing")).contains(QStringLiteral("shippingCents")));
    QVERIFY(names(data.value("missing")).contains(QStringLiteral("location")));
    QVERIFY(names(data.value("missing")).contains(QStringLiteral("soldCount")));

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
    QCOMPARE(data.value("rowCount").toInt(), 3);
    QCOMPARE(data.value("morePossible").toBool(), false);

    // The table header repeats "Avg sold price" and a row cell holds a span
    // "Free shipping"; both are inside the table and must not be used.
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
    QCOMPARE(rows.size(), 3);
    const QJsonObject r1 = rows.at(0).toObject();
    QCOMPARE(r1.value("itemId").toString(), QStringLiteral("100000000020"));
    QCOMPARE(r1.value("title").toString(), QStringLiteral("Acme Model 7 Multimeter"));
    QCOMPARE(r1.value("avgSoldPriceCents").toInt(), 4170);
    QCOMPARE(r1.value("formatText").toString(), QStringLiteral("Fixed price"));
    QCOMPARE(r1.value("avgShippingCents").toInt(), 585);
    QCOMPARE(r1.value("freeShippingPercent").toDouble(), 0.0);
    QCOMPARE(r1.value("totalSold").toInt(), 4);
    QCOMPARE(r1.value("totalSalesCents").toInt(), 16680);
    QCOMPARE(r1.value("bids").toInt(), 0);
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
