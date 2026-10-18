// Extractor tests: each synthetic fixture (tests/fixtures/) is loaded into a
// real QWebEnginePage under the eBay URL it stands for, and the extractor's
// result is checked field by field. Runs with the offscreen platform in CI;
// no network is used (setHtml loads the fixture from memory).
#include "browser/Extractor.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineProfileBuilder>

#include <optional>

using app::browser::Extractor;
using app::ebay::PageKind;

namespace {

const char kActiveUrl[] = "https://www.ebay.com/sch/i.html?_nkw=test+meter&_ipg=240";
const char kSoldUrl[] = "https://www.ebay.com/sch/i.html?_nkw=test+meter&LH_Sold=1&LH_Complete=1&_ipg=240&_pgn=3";
const char kItemUrl[] = "https://www.ebay.com/itm/100000000010";
const char kItemDomUrl[] = "https://www.ebay.com/itm/100000000030?hash=x";
const char kResearchUrl[] = "https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=test+meter"
                            "&dayRange=1095&tabName=SOLD&limit=50&offset=0&tz=America%2FNew_York";
const char kLandingUrl[] = "https://www.ebay.com/sh/landing";
const char kSignInUrl[] = "https://signin.ebay.com/ws/eBayISAPI.dll?SignIn&ru=https%3A%2F%2Fwww.ebay.com%2F";

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
    // Loads `fixture` under `url`, runs the extractor for `kind` and returns
    // its result; std::nullopt (with *error) when loading or the extractor
    // did not finish.
    std::optional<QJsonObject> extract(const char *fixture, const char *url, PageKind kind, QString *error);

    QWebEngineProfile *m_profile = nullptr;
    QWebEnginePage *m_page = nullptr;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void activeSearch();
    void soldSearch();
    void itemFromJsonLd();
    void itemFromDom();
    void research();
    void errorPage_data();
    void errorPage();
    void unexpectedPage_data();
    void unexpectedPage();
    void researchLandingMeansNoAccess();
    void landingIsUnexpectedForOtherKinds();
    void signInPage();
    void parseResultRejectsInvalidResults();
};

std::optional<QJsonObject> TestExtractors::extract(const char *fixture, const char *url, PageKind kind,
                                                   QString *error)
{
    QFile file(QStringLiteral(":/fixtures/") + QString::fromLatin1(fixture));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("fixture not found");
        return std::nullopt;
    }
    QSignalSpy loaded(m_page, &QWebEnginePage::loadFinished);
    m_page->setHtml(QString::fromUtf8(file.readAll()), QUrl(QString::fromLatin1(url)));
    if (!loaded.wait(30000)) {
        *error = QStringLiteral("the fixture did not load");
        return std::nullopt;
    }
    if (m_page->url() != QUrl(QString::fromLatin1(url))) {
        *error = QStringLiteral("page URL is %1, not the fixture URL").arg(m_page->url().toString());
        return std::nullopt;
    }
    std::optional<QJsonObject> result;
    Extractor::run(m_page, kind, [&result](const QJsonObject &value) { result = value; });
    if (!QTest::qWaitFor([&result]() { return result.has_value(); }, 30000)) {
        *error = QStringLiteral("the extractor did not answer");
        return std::nullopt;
    }
    return result;
}

void TestExtractors::initTestCase()
{
    m_profile = QWebEngineProfileBuilder::createOffTheRecordProfile();
    QVERIFY(m_profile);
    m_page = new QWebEnginePage(m_profile);
}

void TestExtractors::cleanupTestCase()
{
    delete m_page;
    m_page = nullptr;
    delete m_profile;
    m_profile = nullptr;
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
    QCOMPARE(a.value("price").toObject().value("minCents").toInt(), 30000);
    QCOMPARE(a.value("price").toObject().value("maxCents").toInt(), 30000);
    QCOMPARE(a.value("price").toObject().value("currency").toString(), QStringLiteral("USD"));
    QCOMPARE(a.value("priceText").toString(), QStringLiteral("$300.00"));
    QCOMPARE(a.value("shippingCents").toInt(), 1299);
    QCOMPARE(a.value("format").toString(), QStringLiteral("fixed_price"));
    QVERIFY(a.value("bids").isNull());
    QCOMPARE(a.value("buyItNow").toBool(), false);
    QCOMPARE(a.value("bestOfferOffered").toBool(), true);
    QCOMPARE(a.value("bestOfferAccepted").toBool(), false);
    QCOMPARE(a.value("freeReturns").toBool(), true);
    QCOMPARE(a.value("location").toString(), QStringLiteral("United States"));
    QCOMPARE(a.value("listedAt").toString(), QStringLiteral("Oct-18 11:32"));
    QCOMPARE(a.value("seller").toObject().value("name").toString(), QStringLiteral("examplesellerone"));
    QCOMPARE(a.value("seller").toObject().value("positivePercent").toDouble(), 99.8);
    QCOMPARE(a.value("seller").toObject().value("feedbackCount").toInt(), 983);
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

    const QJsonObject c = items.at(2).toObject();
    QCOMPARE(c.value("itemId").toString(), QStringLiteral("100000000003"));
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
    QCOMPARE(data.value("totalResults").toInt(), 535);
    QCOMPARE(data.value("page").toInt(), 3);
    QCOMPARE(data.value("hasMorePages").toBool(), false);
    QCOMPARE(data.value("skippedDummyCards").toInt(), 1);
    QCOMPARE(data.value("unrecognizedRows").toInt(), 0);
    const QJsonArray items = data.value("items").toArray();
    QCOMPARE(items.size(), 3);

    // Best offer accepted: the shown price is the asking price -> unconfirmed.
    const QJsonObject a = items.at(0).toObject();
    QCOMPARE(a.value("soldDate").toString(), QStringLiteral("2026-10-18"));
    QCOMPARE(a.value("price").toObject().value("minCents").toInt(), 9500);
    QCOMPARE(a.value("bestOfferAccepted").toBool(), true);
    QCOMPARE(a.value("priceStatus").toString(), QStringLiteral("unconfirmed"));
    QCOMPARE(a.value("format").toString(), QStringLiteral("fixed_price"));
    QCOMPARE(a.value("shippingCents").toInt(), 913);
    QCOMPARE(a.value("seller").toObject().value("positivePercent").toDouble(), 99.0);
    QCOMPARE(names(a.value("missing")), set({"bids", "listedAt"}));

    const QJsonObject b = items.at(1).toObject();
    QCOMPARE(b.value("soldDate").toString(), QStringLiteral("2026-10-12"));
    QCOMPARE(b.value("bids").toInt(), 40);
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
    QCOMPARE(data.value("itemId").toString(), QStringLiteral("100000000010"));
    QCOMPARE(data.value("title").toString(), QStringLiteral("Acme Model 7 Digital Multimeter (JSON-LD title)"));
    QCOMPARE(data.value("titleSource").toString(), QStringLiteral("json-ld"));
    QCOMPARE(data.value("priceCents").toInt(), 26999);
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
    QCOMPARE(names(data.value("missing")), QSet<QString>());

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
    QCOMPARE(data.value("itemId").toString(), QStringLiteral("100000000030"));
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
             set({"availability", "listingStartedAt", "returnsText", "seller", "watchers", "soldCount"}));
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
    QCOMPARE(summary.value("avgSoldPriceCents").toInt(), 3816);
    QCOMPARE(summary.value("soldPriceRange").toObject().value("minCents").toInt(), 1200);
    QCOMPARE(summary.value("soldPriceRange").toObject().value("maxCents").toInt(), 8050);
    QCOMPARE(summary.value("avgShippingCents").toInt(), 663);
    QCOMPARE(summary.value("freeShippingPercent").toDouble(), 45.0);
    QVERIFY(summary.value("sellThroughPercent").isNull());
    QCOMPARE(summary.value("totalSellers").toInt(), 1021);
    QCOMPARE(names(summary.value("missing")), set({"sellThroughPercent"}));

    const QJsonArray rows = data.value("rows").toArray();
    QCOMPARE(rows.size(), 3);
    const QJsonObject r1 = rows.at(0).toObject();
    QCOMPARE(r1.value("itemId").toString(), QStringLiteral("100000000020"));
    QCOMPARE(r1.value("title").toString(), QStringLiteral("Acme Model 7 Multimeter"));
    QCOMPARE(r1.value("avgSoldPriceCents").toInt(), 3816);
    QCOMPARE(r1.value("formatText").toString(), QStringLiteral("Fixed price"));
    QCOMPARE(r1.value("avgShippingCents").toInt(), 663);
    QCOMPARE(r1.value("freeShippingPercent").toDouble(), 0.0);
    QCOMPARE(r1.value("totalSold").toInt(), 4);
    QCOMPARE(r1.value("totalSalesCents").toInt(), 15264);
    QCOMPARE(r1.value("bids").toInt(), 0);
    QCOMPARE(r1.value("lastSoldDate").toString(), QStringLiteral("2026-10-12"));
    QCOMPARE(names(r1.value("missing")), QSet<QString>());

    // A row with only the item number in place of a title.
    const QJsonObject r2 = rows.at(1).toObject();
    QCOMPARE(r2.value("itemId").toString(), QStringLiteral("100000000021"));
    QVERIFY(r2.value("title").isNull());
    QCOMPARE(r2.value("avgSoldPriceCents").toInt(), 2888);
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
    errorPage_data();
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

void TestExtractors::researchLandingMeansNoAccess()
{
    QString error;
    const std::optional<QJsonObject> result = extract("check_page.html", kLandingUrl, PageKind::Research, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("no_access"));
}

void TestExtractors::landingIsUnexpectedForOtherKinds()
{
    QString error;
    const std::optional<QJsonObject> result = extract("check_page.html", kLandingUrl, PageKind::Item, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("unexpected"));
}

void TestExtractors::signInPage()
{
    QString error;
    const std::optional<QJsonObject> result = extract("check_page.html", kSignInUrl, PageKind::ActiveSearch, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->value("page").toString(), QStringLiteral("sign_in"));
}

void TestExtractors::parseResultRejectsInvalidResults()
{
    QCOMPARE(Extractor::parseResult(QVariant(42)).value("page").toString(), QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QVariant(QStringLiteral("not json"))).value("page").toString(),
             QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QVariant(QStringLiteral("[1,2]"))).value("page").toString(),
             QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QVariant(QStringLiteral("{\"page\":\"something\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    QCOMPARE(Extractor::parseResult(QVariant(QStringLiteral("{\"page\":\"expected\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    // A page script cannot claim the URL-based outcomes.
    QCOMPARE(Extractor::parseResult(QVariant(QStringLiteral("{\"page\":\"no_access\"}"))).value("page").toString(),
             QStringLiteral("invalid"));
    const QJsonObject ok = Extractor::parseResult(QVariant(QStringLiteral("{\"page\":\"expected\",\"data\":{\"a\":1}}")));
    QCOMPARE(ok.value("page").toString(), QStringLiteral("expected"));
    QCOMPARE(ok.value("data").toObject().value("a").toInt(), 1);
}

int main(int argc, char *argv[])
{
    // Required by Qt WebEngine before the application object exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);
    TestExtractors test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_extractors.moc"
