// The lookup flow without a browser (src/lookup/): the percentile and money
// helpers, records from extractor JSON, and LookupRunner driven with fake
// page replies (the reply objects of BrowserController), with the built-in
// matching data. Identity, matching and value: tst_matching. Every item,
// seller and number is invented.
#include "core/Settings.h"
#include "ebay/SearchUrls.h"
#include "lookup/AuctionTime.h"
#include "lookup/Identity.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"
#include "lookup/Records.h"
#include "lookup/Stats.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QUrlQuery>

using namespace app::lookup;
using app::ebay::PageKind;

namespace {

QJsonObject okReply(const QJsonObject &result)
{
    return QJsonObject{{"status", "ok"}, {"result", result}};
}

// An item extractor result.
QJsonObject itemData()
{
    return QJsonObject{{"itemId", "100000000050"},
                       {"title", "Acme M7X 4-1/2 Digit Meter 1000V"},
                       {"priceCents", 25000},
                       {"currency", "USD"},
                       {"shippingCents", 1500},
                       {"condition", "UsedCondition"},
                       {"conditionText", "Parts Only"},
                       {"seller", QJsonObject{{"name", "examplestore"}}},
                       {"specifics", QJsonArray{QJsonObject{{"label", "Brand"}, {"value", "Acme"}},
                                                QJsonObject{{"label", "Model"}, {"value", "Acme M7-X"}},
                                                QJsonObject{{"label", "MPN"}, {"value", "M7X/B2"}}}}};
}

// One search card: `number` numbers the item, `cents` is its price.
QJsonObject card(int number, qint64 cents, qint64 shipping, bool soldConfirmed = true)
{
    QJsonObject item{{"itemId", QStringLiteral("2000000%1").arg(number, 5, 10, QLatin1Char('0'))},
                     {"title", QStringLiteral("Acme M7X meter %1").arg(number)},
                     {"price", QJsonObject{{"minCents", cents}, {"maxCents", cents}, {"currency", "USD"}}},
                     {"priceText", "x"},
                     {"shippingCents", shipping},
                     {"format", "fixed_price"},
                     {"soldDate", "2026-10-01"},
                     {"priceStatus", soldConfirmed ? "shown" : "unconfirmed"}};
    return item;
}

// A search extractor result with cards numbered first..first+count-1.
QJsonObject searchPage(int first, int count, bool more, qint64 cents = 10000)
{
    QJsonArray items;
    for (int i = 0; i < count; ++i)
        items.append(card(first + i, cents, 500));
    return QJsonObject{{"items", items}, {"hasMorePages", more}};
}

QJsonObject researchPage(int first, int count, bool more)
{
    QJsonArray rows;
    for (int i = 0; i < count; ++i) {
        rows.append(QJsonObject{{"itemId", QStringLiteral("3000000%1").arg(first + i, 5, 10, QLatin1Char('0'))},
                                {"title", "Acme M7X"},
                                {"avgSoldPriceCents", 9000},
                                {"avgShippingCents", 0},
                                {"totalSold", 2},
                                {"lastSoldDate", "2026-10-02"}});
    }
    return QJsonObject{{"rows", rows}, {"morePossible", more}, {"summary", QJsonObject{{"avgSoldPriceCents", 9100}}}};
}

struct Request {
    QUrl url;
    PageKind kind;
};

} // namespace

class TestLookup : public QObject {
    Q_OBJECT

private:
    // No known brands: "Acme" is taken as a new brand from the Brand specific.
    app::lookup::KnownModels m_known;
    app::lookup::MatchWords m_words;
    // A runner whose fetches are recorded in m_requests; replies are given
    // by the test with onReply().
    LookupRunner *makeRunner();
    // Starts a runner and answers the item page; the runner then waits for
    // queries.
    LookupRunner *runnerWithItem();

    QList<Request> m_requests;

private slots:
    void initTestCase();
    void init();
    void itemInput_data();
    void itemInput();
    void searchUrls();
    void percentiles();
    void moneyText();
    void recordsFromJson();
    void fullLookup();
    void researchWithoutAccess();
    void stopsOnUnexpectedPage();
    void repeatsAcrossPagesAreSkipped();
    void unknownPageCountStopsPaging();
    void itemNumberIsNotFilledIn();
    void stopsAtDailyCap();
    void cancelStopsAfterCurrentPage();
    void cancelWhileWaitingForQueries();
    void browserCancelEndsAsCancelled();
    void refusesBadInput();
    void startFromSavedItem();
    void auctionEnds();
    void countdownText();
    void reportNumbers();
};

void TestLookup::initTestCase()
{
    QString error;
    const std::optional<MatchWords> words = MatchWords::builtIn(&error);
    QVERIFY2(words, qPrintable(error));
    m_words = *words;
}

void TestLookup::init()
{
    m_requests.clear();
}

LookupRunner *TestLookup::makeRunner()
{
    return new LookupRunner([this](const QUrl &url, PageKind kind) { m_requests.append({url, kind}); }, &m_known, &m_words,
                            this);
}

LookupRunner *TestLookup::runnerWithItem()
{
    LookupRunner *runner = makeRunner();
    QString error;
    if (!runner->startItem(QStringLiteral("https://www.ebay.com/itm/100000000050?hash=x"), &error))
        return nullptr;
    runner->onReply(okReply(itemData()));
    return runner;
}

void TestLookup::itemInput_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("itemId");
    QTest::newRow("address") << "https://www.ebay.com/itm/100000000050" << "100000000050";
    QTest::newRow("address with query") << "  https://www.ebay.com/itm/100000000050?hash=item1&var=2 " << "100000000050";
    QTest::newRow("bare number") << "100000000050" << "100000000050";
    QTest::newRow("11 digits") << "10000000005" << "";
    QTest::newRow("http") << "http://www.ebay.com/itm/100000000050" << "";
    QTest::newRow("other site") << "https://www.example.com/itm/100000000050" << "";
    QTest::newRow("search page") << "https://www.ebay.com/sch/i.html?_nkw=x" << "";
    QTest::newRow("text") << "acme m7x" << "";
}

void TestLookup::itemInput()
{
    QFETCH(QString, input);
    QFETCH(QString, itemId);
    QCOMPARE(app::ebay::itemIdFromInput(input), itemId);
}

void TestLookup::searchUrls()
{
    QCOMPARE(app::ebay::soldSearchUrl(QStringLiteral(" Acme  M7X "), 1).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=Acme+M7X&LH_Sold=1&LH_Complete=1&_ipg=240"));
    QCOMPARE(app::ebay::activeSearchUrl(QStringLiteral("M7X & co"), 3).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=M7X+%26+co&_ipg=240&_pgn=3"));
    QCOMPARE(app::ebay::researchUrl(QStringLiteral("Acme M7X"), 90, 50).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=Acme%20M7X&dayRange=90"
                            "&tabName=SOLD&limit=50&offset=50"));
    QCOMPARE(app::ebay::itemUrl(QStringLiteral("100000000050")).toString(), QStringLiteral("https://www.ebay.com/itm/100000000050"));
}

void TestLookup::percentiles()
{
    const QList<qint64> values{400, 100, 300, 200};
    QCOMPARE(percentile(values, 25), std::optional<qint64>(175));
    QCOMPARE(percentile(values, 50), std::optional<qint64>(250));
    QCOMPARE(percentile(values, 75), std::optional<qint64>(325));
    QCOMPARE(percentile(values, 0), std::optional<qint64>(100));
    QCOMPARE(percentile(values, 100), std::optional<qint64>(400));
    QCOMPARE(percentile({500}, 25), std::optional<qint64>(500));
    // Half a cent rounds away from zero.
    QCOMPARE(percentile({100, 101}, 50), std::optional<qint64>(101));
    QCOMPARE(percentile({}, 50), std::nullopt);
    QCOMPARE(percentile(values, 101), std::nullopt);
    QCOMPARE(percentile(values, -1), std::nullopt);
}

void TestLookup::moneyText()
{
    QCOMPARE(money(0), QStringLiteral("$0.00"));
    QCOMPARE(money(5), QStringLiteral("$0.05"));
    QCOMPARE(money(99999), QStringLiteral("$999.99"));
    QCOMPARE(money(123456), QStringLiteral("$1,234.56"));
    QCOMPARE(money(100000000), QStringLiteral("$1,000,000.00"));
    QCOMPARE(money(-1200), QStringLiteral("-$12.00"));
}

void TestLookup::recordsFromJson()
{
    const SearchRecord confirmed = SearchRecord::fromJson(card(1, 10000, 500), true);
    QCOMPARE(confirmed.priceCents, std::optional<qint64>(10000));
    QCOMPARE(confirmed.totalCents(), std::optional<qint64>(10500));
    QVERIFY(confirmed.confirmed);
    QCOMPARE(confirmed.soldDate, QStringLiteral("2026-10-01"));
    QVERIFY(!SearchRecord::fromJson(card(1, 10000, 500, false), true).confirmed);
    // Active search: no sold fields.
    const SearchRecord active = SearchRecord::fromJson(card(1, 10000, 500), false);
    QVERIFY(!active.confirmed);
    QVERIFY(active.soldDate.isEmpty());
    // The card's "Located in" text, kept as given; none shown: empty; an
    // overlong text is not kept.
    QVERIFY(active.location.isEmpty());
    QJsonObject located = card(3, 10000, 500);
    located.insert("location", "  China ");
    QCOMPARE(SearchRecord::fromJson(located, false).location, QStringLiteral("China"));
    located.insert("location", QString(101, QLatin1Char('a')));
    QVERIFY(SearchRecord::fromJson(located, false).location.isEmpty());

    QJsonObject range = card(2, 0, 500);
    range.insert("price", QJsonObject{{"minCents", 1000}, {"maxCents", 2000}, {"currency", "USD"}});
    range.insert("shippingCents", QJsonValue());
    const SearchRecord ranged = SearchRecord::fromJson(range, true);
    QCOMPARE(ranged.priceCents, std::nullopt);
    QCOMPARE(ranged.shippingCents, std::nullopt);
    QCOMPARE(ranged.totalCents(), std::nullopt);

    QJsonObject fraction = card(3, 0, 500);
    fraction.insert("price", QJsonObject{{"minCents", 10.5}, {"maxCents", 10.5}, {"currency", "USD"}});
    QCOMPARE(SearchRecord::fromJson(fraction, true).priceCents, std::nullopt);

    const ItemRecord item = ItemRecord::fromJson(itemData());
    QCOMPARE(item.totalCents(), std::optional<qint64>(26500));
    QCOMPARE(item.specific(QStringLiteral("mpn")), QStringLiteral("M7X/B2"));
    QVERIFY(item.location.isEmpty());
    QJsonObject locatedItem = itemData();
    locatedItem.insert("location", "Exampletown, Guangdong, China");
    QCOMPARE(ItemRecord::fromJson(locatedItem).location, QStringLiteral("Exampletown, Guangdong, China"));
    // Sold: the notice and OutOfStock together (recon 2026-10-21); either alone
    // is not "sold". The description and why it is missing are kept.
    QVERIFY(!item.sold());
    QJsonObject soldItem = itemData();
    soldItem.insert("availability", "OutOfStock");
    QVERIFY(!ItemRecord::fromJson(soldItem).sold());
    soldItem.insert("soldNotice", true);
    QVERIFY(ItemRecord::fromJson(soldItem).sold());
    soldItem.insert("availability", "InStock");
    QVERIFY(!ItemRecord::fromJson(soldItem).sold());
    soldItem.insert("description", QString(25000, QLatin1Char('a')));
    QCOMPARE(ItemRecord::fromJson(soldItem).description.size(), 20000);
    soldItem.remove("description");
    soldItem.insert("descriptionMissing", "no description frame on the page");
    QCOMPARE(ItemRecord::fromJson(soldItem).descriptionMissing, QStringLiteral("no description frame on the page"));
    QJsonObject euro = itemData();
    euro.insert("currency", "EUR");
    QCOMPARE(ItemRecord::fromJson(euro).priceCents, std::nullopt);
}

void TestLookup::fullLookup()
{
    LookupRunner *runner = makeRunner();
    QSignalSpy itemReady(runner, &LookupRunner::itemReady);
    QSignalSpy finished(runner, &LookupRunner::finished);
    QString error;
    QVERIFY(runner->startItem(QStringLiteral("100000000050"), &error));
    QCOMPARE(m_requests.size(), 1);
    QCOMPARE(m_requests.at(0).url.toString(), QStringLiteral("https://www.ebay.com/itm/100000000050"));
    QCOMPARE(m_requests.at(0).kind, PageKind::Item);
    runner->onReply(okReply(itemData()));
    QCOMPARE(itemReady.size(), 1);
    QVERIFY(runner->isWaitingForQueries());
    // Brand "Acme" (a new brand, as written) and the model right after the
    // brand in the listing's own title (owner case 11: the title before the
    // Model specific "Acme M7-X", the same code): one query (owner rule 21).
    QVERIFY(runner->result().identity.confident);
    QCOMPARE(runner->result().queries, QStringList({"Acme M7X"}));

    // The user keeps two queries (and clears the third).
    QVERIFY(runner->startComps({QStringLiteral("Acme M7X"), QStringLiteral("M7X"), QStringLiteral("  ")},
                               Depths{300, 240, 60}, 90, &error));
    QCOMPARE(runner->result().queries, QStringList({"Acme M7X", "M7X"}));

    // Sold: query 1 page 1 (240, more), page 2 (100 read, 60 taken) -> depth 300 reached.
    QCOMPARE(m_requests.size(), 2);
    QCOMPARE(m_requests.at(1).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=Acme+M7X&LH_Sold=1&LH_Complete=1&_ipg=240"));
    QCOMPARE(m_requests.at(1).kind, PageKind::SoldSearch);
    runner->onReply(okReply(searchPage(1, 240, true)));
    QCOMPARE(m_requests.at(2).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=Acme+M7X&LH_Sold=1&LH_Complete=1&_ipg=240&_pgn=2"));
    runner->onReply(okReply(searchPage(241, 100, true)));
    QCOMPARE(runner->result().sold.size(), 300);

    // Active: query 1 has 10 (no more pages); query 2 repeats 2 of them and adds 3.
    QCOMPARE(m_requests.at(3).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=Acme+M7X&_ipg=240"));
    QCOMPARE(m_requests.at(3).kind, PageKind::ActiveSearch);
    runner->onReply(okReply(searchPage(1, 10, false)));
    QCOMPARE(m_requests.at(4).url.toString(QUrl::FullyEncoded), QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=M7X&_ipg=240"));
    runner->onReply(okReply(searchPage(9, 5, false)));
    QCOMPARE(runner->result().active.size(), 13);

    // Research: 50 rows (more), then 50 more of which 10 fit the depth of 60.
    QCOMPARE(m_requests.at(5).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=Acme%20M7X&dayRange=90"
                            "&tabName=SOLD&limit=50&offset=0"));
    QCOMPARE(m_requests.at(5).kind, PageKind::Research);
    runner->onReply(okReply(researchPage(1, 50, true)));
    QCOMPARE(m_requests.at(6).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=Acme%20M7X&dayRange=90"
                            "&tabName=SOLD&limit=50&offset=50"));
    runner->onReply(okReply(researchPage(51, 50, true)));

    QCOMPARE(m_requests.size(), 7);
    QCOMPARE(finished.size(), 1);
    const LookupResult &result = runner->result();
    QCOMPARE(result.status, QStringLiteral("complete"));
    QCOMPARE(result.research.size(), 60);
    QCOMPARE(result.researchSummary.value("avgSoldPriceCents").toInt(), 9100);
    QCOMPARE(result.pagesLoaded, 7);
    QVERIFY(!runner->isFetching());
}

void TestLookup::researchWithoutAccess()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QSignalSpy finished(runner, &LookupRunner::finished);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X"), QStringLiteral("B2")}, Depths{240, 0, 200}, 90, &error));
    runner->onReply(okReply(searchPage(1, 3, false)));
    runner->onReply(okReply(searchPage(4, 2, false)));
    QCOMPARE(runner->result().sold.size(), 5);
    QVERIFY(runner->result().sold.at(0).confirmed);
    QCOMPARE(m_requests.size(), 4);
    QCOMPARE(m_requests.at(3).kind, PageKind::Research);
    runner->onReply(QJsonObject{{"status", "unexpected_page"}, {"page", "no_access"}, {"reason", "Seller Hub start page"}});
    // Research is skipped for every query; the lookup still completes, and
    // (owner decision Q22) every sold price is now unconfirmed.
    QCOMPARE(m_requests.size(), 4);
    for (const SearchRecord &record : runner->result().sold)
        QVERIFY(!record.confirmed);
    const QList<Row> rows = classifyRows(runner->result(), m_words);
    const Valuation valuation = valuate(runner->result(), rows, app::ValueSettings());
    QCOMPARE(valuation.sold.included, 0);
    QCOMPARE(valuation.sold.medianCents, std::nullopt);
    QCOMPARE(valuation.sold.unconfirmed, 5);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(runner->result().status, QStringLiteral("complete"));
    QVERIFY(runner->result().researchUnavailable);
    QCOMPARE(runner->result().notes.size(), 1);
}

void TestLookup::stopsOnUnexpectedPage()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QSignalSpy finished(runner, &LookupRunner::finished);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X")}, Depths{240, 240, 200}, 90, &error));
    runner->onReply(okReply(searchPage(1, 3, false)));
    runner->onReply(QJsonObject{{"status", "unexpected_page"}, {"page", "sign_in"}, {"reason", "eBay showed its sign-in page"}});
    QCOMPARE(m_requests.size(), 3);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(runner->result().status, QStringLiteral("stopped"));
    QVERIFY2(runner->result().message.contains(QStringLiteral("for sale now page not read: eBay showed sign_in")),
             qPrintable(runner->result().message));
    // What was collected before stays.
    QCOMPARE(runner->result().sold.size(), 3);
}

void TestLookup::repeatsAcrossPagesAreSkipped()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X")}, Depths{480, 0, 0}, 90, &error));
    // Page 2 starts with the last 2 cards of page 1 (the pages shifted).
    runner->onReply(okReply(searchPage(1, 240, true)));
    runner->onReply(okReply(searchPage(239, 10, false)));
    QCOMPARE(runner->result().sold.size(), 248);
    // The same listing twice on one page is kept (separate sales possible).
    LookupRunner *again = runnerWithItem();
    QVERIFY(again);
    QVERIFY(again->startComps({QStringLiteral("M7X")}, Depths{480, 0, 0}, 90, &error));
    QJsonObject page = searchPage(1, 2, false);
    QJsonArray items = page.value("items").toArray();
    items.append(items.at(0));
    page.insert("items", items);
    again->onReply(okReply(page));
    QCOMPARE(again->result().sold.size(), 3);
}

void TestLookup::unknownPageCountStopsPaging()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X"), QStringLiteral("B2")}, Depths{480, 0, 0}, 90, &error));
    QJsonObject page = searchPage(1, 240, true);
    page.insert("hasMorePages", QJsonValue()); // the extractor could not tell
    runner->onReply(okReply(page));
    // Query 1 stops after this page with a note; query 2 comes next.
    QCOMPARE(m_requests.size(), 3);
    QCOMPARE(m_requests.at(2).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=B2&LH_Sold=1&LH_Complete=1&_ipg=240"));
    QCOMPARE(runner->result().notes.size(), 1);
    QVERIFY(runner->result().notes.at(0).contains(QStringLiteral("does not tell whether more results exist")));
}

void TestLookup::itemNumberIsNotFilledIn()
{
    LookupRunner *runner = makeRunner();
    QString error;
    QVERIFY(runner->startItem(QStringLiteral("100000000050"), &error));
    QJsonObject data = itemData();
    data.insert("itemId", QJsonValue());
    runner->onReply(okReply(data));
    QVERIFY(runner->result().item.itemId.isEmpty());
    QCOMPARE(runner->result().notes.size(), 1);
    QVERIFY(runner->result().notes.at(0).contains(QStringLiteral("asked for 100000000050")));
}

void TestLookup::stopsAtDailyCap()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X")}, Depths{240, 0, 0}, 90, &error));
    runner->onReply(QJsonObject{{"status", "limit_reached"}, {"error", "Today's page limit is reached"}});
    QCOMPARE(runner->result().status, QStringLiteral("stopped"));
    QVERIFY(runner->result().message.contains(QStringLiteral("Today's page limit is reached")));
    // A refused load is not a loaded page: only the item page counts.
    QCOMPARE(runner->result().pagesLoaded, 1);
}

void TestLookup::cancelStopsAfterCurrentPage()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X")}, Depths{480, 240, 200}, 90, &error));
    QVERIFY(runner->isFetching());
    runner->cancel();
    QCOMPARE(runner->result().status, QStringLiteral("running"));
    runner->onReply(okReply(searchPage(1, 240, true)));
    QCOMPARE(runner->result().status, QStringLiteral("cancelled"));
    QCOMPARE(runner->result().sold.size(), 240);
    QCOMPARE(m_requests.size(), 2);
}

void TestLookup::cancelWhileWaitingForQueries()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QSignalSpy finished(runner, &LookupRunner::finished);
    runner->cancel();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(runner->result().status, QStringLiteral("cancelled"));
    QString error;
    QVERIFY(!runner->startComps({QStringLiteral("M7X")}, Depths{240, 240, 200}, 90, &error));
    QCOMPARE(m_requests.size(), 1);
}

void TestLookup::browserCancelEndsAsCancelled()
{
    LookupRunner *runner = runnerWithItem();
    QVERIFY(runner);
    QString error;
    QVERIFY(runner->startComps({QStringLiteral("M7X")}, Depths{240, 240, 200}, 90, &error));
    runner->onReply(QJsonObject{{"status", "failed"}, {"error", "stopped"}});
    QCOMPARE(runner->result().status, QStringLiteral("cancelled"));
}

void TestLookup::refusesBadInput()
{
    LookupRunner *runner = makeRunner();
    QString error;
    QVERIFY(!runner->startItem(QStringLiteral("acme m7x"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(m_requests.isEmpty());
    // No queries before the listing is read.
    QVERIFY(!runner->startComps({QStringLiteral("M7X")}, Depths{240, 240, 200}, 90, &error));

    LookupRunner *withItem = runnerWithItem();
    QVERIFY(withItem);
    QVERIFY(!withItem->startComps({QStringLiteral(" "), QString()}, Depths{240, 240, 200}, 90, &error));
    QVERIFY(!withItem->startComps({QStringLiteral("M7X")}, Depths{-1, 240, 200}, 90, &error));
    QVERIFY(!withItem->startComps({QStringLiteral("M7X")}, Depths{240, 240, 200}, 0, &error));
    QVERIFY(withItem->isWaitingForQueries());

    // An item page that is not read stops the lookup.
    LookupRunner *failing = makeRunner();
    QVERIFY(failing->startItem(QStringLiteral("100000000051"), &error));
    failing->onReply(QJsonObject{{"status", "unexpected_page"}, {"page", "error_page"}, {"reason", "eBay error page"}});
    QCOMPARE(failing->result().status, QStringLiteral("stopped"));
    QVERIFY(failing->result().message.contains(QStringLiteral("error_page")));
}

void TestLookup::reportNumbers()
{
    LookupResult result;
    result.item = ItemRecord::fromJson(itemData()); // total $265.00, shipping $15.00
    result.identity = identityFromUser({QStringLiteral("Acme"), QStringLiteral("M7X"), QString()}, m_known, QStringLiteral("user"));
    // Confirmed totals $105, $205, $305, $405 of the same item; one
    // unconfirmed; one without shipping (no total).
    for (int i = 0; i < 4; ++i)
        result.sold.append(SearchRecord::fromJson(card(i, 10000 + i * 10000, 500), true));
    result.sold.append(SearchRecord::fromJson(card(9, 99900, 500, false), true));
    QJsonObject noShipping = card(10, 50000, 0);
    noShipping.insert("shippingCents", QJsonValue());
    result.sold.append(SearchRecord::fromJson(noShipping, true));
    // For sale now: $305.00 and $155.00 (listed in that order).
    result.active.append(SearchRecord::fromJson(card(21, 30000, 500), false));
    result.active.append(SearchRecord::fromJson(card(20, 15000, 500), false));
    result.status = QStringLiteral("complete");

    QList<Row> rows = classifyRows(result, m_words);
    QCOMPARE(rows.size(), 8);
    for (const Row &row : std::as_const(rows))
        QCOMPARE(row.match.bucket, Bucket::SameItem);
    Valuation valuation = valuate(result, rows, app::ValueSettings());
    const ValueNumbers &level = valuation.sold;
    QCOMPARE(level.included, 4);
    QCOMPARE(level.p25Cents, std::optional<qint64>(18000));
    QCOMPARE(level.medianCents, std::optional<qint64>(25500));
    QCOMPARE(level.p75Cents, std::optional<qint64>(33000));
    QCOMPARE(level.unconfirmed, 1);
    QCOMPARE(valuation.shipping.compsWithShipping, 4);
    QCOMPARE(valuation.shipping.compMedianCents, std::optional<qint64>(500));
    // $15 vs $5: 3 x the median, but only $10 above it, not more: no flag.
    QVERIFY(!valuation.shipping.flagged);

    const QString text = reportText(result, rows, valuation, QStringLiteral("$200-300"));
    QVERIFY2(text.contains(QStringLiteral("4 sales, median $255.00, P25-P75 $180.00-$330.00")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("THIS LISTING: total $265.00 | sold median $255.00 (+$10.00)")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("your estimate: $200-300")), qPrintable(text));
    // eBay's visible condition text, never the schema.org code.
    QVERIFY2(text.contains(QStringLiteral("eBay condition: Parts Only")), qPrintable(text));
    QVERIFY(!text.contains(QStringLiteral("UsedCondition")));
    QVERIFY(text.contains(QStringLiteral("SOLD - same item (6)")));
    // The pasted listing against what is for sale now; lowest total first,
    // the cheapest marked.
    QVERIFY2(text.contains(QStringLiteral("for sale now: 1 of 2 same-item listings are cheaper, cheapest $155.00")), qPrintable(text));
    const qsizetype cheap = text.indexOf(QStringLiteral("CHEAPEST 1 Acme M7X meter 20"));
    const qsizetype dear = text.indexOf(QStringLiteral("CHEAPEST 2 Acme M7X meter 21"));
    QVERIFY2(cheap > 0 && dear > cheap, qPrintable(text));

    // Unticking a row changes the numbers.
    rows[0].included = false;
    valuation = valuate(result, rows, app::ValueSettings());
    QCOMPARE(valuation.sold.included, 3);
}

// "Wrong item? Correct it" on a saved lookup: the saved listing record is
// identified again without loading the item page, itemReady() comes from
// the event loop, and the searches then run as usual. A cancel before the
// event loop ran ends it without itemReady().
void TestLookup::startFromSavedItem()
{
    const ItemRecord item = ItemRecord::fromJson(itemData());
    QCOMPARE(item.imageUrl, QString());
    LookupRunner *runner = makeRunner();
    QSignalSpy ready(runner, &LookupRunner::itemReady);
    QString error;
    QVERIFY2(runner->startFromItem(item, &error), qPrintable(error));
    QVERIFY(m_requests.isEmpty());
    QCOMPARE(ready.count(), 0);
    QVERIFY(runner->isWaitingForQueries());
    QVERIFY(ready.wait(5000));
    QCOMPARE(runner->result().item.itemId, QStringLiteral("100000000050"));
    QVERIFY(runner->result().identity.confident);
    QVERIFY(!runner->result().queries.isEmpty());
    QVERIFY2(runner->startComps(runner->result().queries, Depths{10, 0, 0}, 90, &error), qPrintable(error));
    QCOMPARE(m_requests.size(), 1);
    QCOMPARE(m_requests.first().kind, PageKind::SoldSearch);
    QVERIFY(!runner->startFromItem(item, &error));

    LookupRunner *cancelled = makeRunner();
    QSignalSpy cancelledReady(cancelled, &LookupRunner::itemReady);
    QSignalSpy finished(cancelled, &LookupRunner::finished);
    QVERIFY(cancelled->startFromItem(item, &error));
    cancelled->cancel();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(cancelled->result().status, QStringLiteral("cancelled"));
    QTest::qWait(50);
    QCOMPARE(cancelledReady.count(), 0);

    ItemRecord noNumber = item;
    noNumber.itemId.clear();
    QVERIFY(!makeRunner()->startFromItem(noNumber, &error));

    // Product Research is asked for 3 years by default (owner decision
    // 2026-10-19).
    QCOMPARE(app::Settings::kDefaultSoldHistoryDays, 1095);
    LookupRunner *research = makeRunner();
    QVERIFY(research->startFromItem(item, &error));
    QVERIFY(QSignalSpy(research, &LookupRunner::itemReady).wait(5000));
    m_requests.clear();
    QVERIFY2(research->startComps(research->result().queries, Depths{0, 0, 50}, app::Settings::kDefaultSoldHistoryDays, &error),
             qPrintable(error));
    QCOMPARE(m_requests.size(), 1);
    QCOMPARE(m_requests.first().kind, PageKind::Research);
    QCOMPARE(QUrlQuery(m_requests.first().url).queryItemValue(QStringLiteral("dayRange")), QStringLiteral("1095"));

    QJsonObject withPhoto = itemData();
    withPhoto.insert("imageUrl", "https://images.example.com/m7x.jpg");
    QCOMPARE(ItemRecord::fromJson(withPhoto).imageUrl, QStringLiteral("https://images.example.com/m7x.jpg"));
}

// The end of an auction from what eBay shows (lookup/AuctionTime): the
// weekday and time count only inside the window the relative text gives.
void TestLookup::auctionEnds()
{
    // Monday 2026-10-19 19:47 local; "3d 22h" + "Friday, 06:28 PM" = Friday
    // 2026-10-23 18:28 local (3d 22h 41m later).
    const QDateTime now(QDate(2026, 10, 19), QTime(19, 47), QTimeZone::LocalTime);
    std::optional<AuctionEnd> end = auctionEnd(QStringLiteral("3d 22h"), QStringLiteral("Friday, 06:28 PM"), now);
    QVERIFY(end);
    QCOMPARE(end->end, QDateTime(QDate(2026, 10, 23), QTime(18, 28), QTimeZone::LocalTime).toUTC());
    QCOMPARE(end->seconds, false);
    // With the label, and the search card's form.
    QVERIFY(auctionEnd(QStringLiteral("Ends in 3d 22h"), QStringLiteral("Friday, 06:28 PM"), now));
    end = auctionEnd(QStringLiteral("3d 17h left"), QStringLiteral("(Fri, 01:11 PM)"), now);
    QVERIFY(end);
    QCOMPARE(end->end, QDateTime(QDate(2026, 10, 23), QTime(13, 11), QTimeZone::LocalTime).toUTC());
    // The two texts disagree: no end.
    QVERIFY(!auctionEnd(QStringLiteral("1d 2h"), QStringLiteral("Friday, 06:28 PM"), now));
    // Down to the second without a weekday: that is enough.
    end = auctionEnd(QStringLiteral("45m 12s"), QString(), now);
    QVERIFY(end);
    QCOMPARE(end->end, now.addSecs(45 * 60 + 12).toUTC());
    QCOMPARE(end->seconds, true);
    // Hours only and no weekday, or nothing readable: no end.
    QVERIFY(!auctionEnd(QStringLiteral("3d 22h"), QString(), now));
    QVERIFY(!auctionEnd(QStringLiteral("soon"), QStringLiteral("Friday, 06:28 PM"), now));
    QVERIFY(!auctionEnd(QString(), QStringLiteral("Friday, 06:28 PM"), now));
    QVERIFY(!auctionEnd(QStringLiteral("3d 22h"), QStringLiteral("Funday, 06:28 PM"), now));
}

void TestLookup::countdownText()
{
    const QDateTime now(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC);
    QCOMPARE(timeLeftText(now.addSecs(2 * 86400 + 5 * 3600 + 12 * 60 + 30), false, now), QStringLiteral("2d 5h 12m left"));
    QCOMPARE(timeLeftText(now.addSecs(5 * 3600 + 12 * 60), true, now), QStringLiteral("5h 12m left"));
    QCOMPARE(timeLeftText(now.addSecs(12 * 60 + 5), true, now), QStringLiteral("12m 05s left"));
    QCOMPARE(timeLeftText(now.addSecs(12 * 60 + 5), false, now), QStringLiteral("12m left"));
    QCOMPARE(timeLeftText(now, true, now), QStringLiteral("ENDED"));
    QCOMPARE(timeLeftText(now.addSecs(-60), false, now), QStringLiteral("ENDED"));
}

QTEST_GUILESS_MAIN(TestLookup)
#include "tst_lookup.moc"
