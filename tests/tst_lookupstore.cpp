// Saved lookups (src/lookup/LookupStore, schema version 2): a lookup run on
// fake page replies -> rows -> numbers -> database -> read back unchanged;
// the user's checkboxes and the numbers stored; the recent list; identities
// and photos; and the upgrade of a synthetic version-1 database to version 2
// (migration 0002_saved_lookups.sql) with the existing backup. Every item,
// seller and number is invented.
#include "db/Database.h"
#include "db/Migrations.h"
#include "lookup/History.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"
#include "lookup/LookupStore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace app::lookup;
using app::Database;
using app::ebay::PageKind;

namespace {

QJsonObject okReply(const QJsonObject &result)
{
    return QJsonObject{{"status", "ok"}, {"result", result}};
}

QJsonObject itemData()
{
    return QJsonObject{{"itemId", "100000000070"},
                       {"title", "Acme Q9 Signal Tracer"},
                       {"imageUrl", "https://images.example.com/q9.jpg"},
                       {"priceCents", 12000},
                       {"currency", "USD"},
                       {"shippingCents", 4500},
                       {"conditionText", "Used"},
                       {"seller", QJsonObject{{"name", "examplestore"}, {"feedbackCount", 7}, {"positivePercent", 98.5}}},
                       {"specifics", QJsonArray{QJsonObject{{"label", "Brand"}, {"value", "Acme"}},
                                                QJsonObject{{"label", "Model"}, {"value", "Q9"}}}}};
}

QJsonObject card(const QString &itemId, const QString &title, qint64 cents, qint64 shipping, bool confirmed,
                 const QString &condition)
{
    return QJsonObject{{"itemId", itemId},
                       {"title", title},
                       {"condition", condition},
                       {"price", QJsonObject{{"minCents", cents}, {"maxCents", cents}, {"currency", "USD"}}},
                       {"priceText", QStringLiteral("$%1").arg(cents / 100)},
                       {"shippingCents", shipping},
                       {"format", "fixed_price"},
                       {"soldDate", "2026-10-03"},
                       {"sellerName", "seller-a"},
                       {"priceStatus", confirmed ? "shown" : "unconfirmed"}};
}

// Sold: four same-item sales (one unconfirmed), one manual. For sale: the
// pasted listing, two cheaper same-item listings, a part. Research: two
// same-item listings.
QJsonObject soldPage()
{
    return QJsonObject{{"items", QJsonArray{card("200000000001", "Acme Q9 Signal Tracer", 8000, 1000, true, "Used"),
                                            card("200000000002", "Acme Q9 tracer tested", 9000, 1000, true, "Used"),
                                            card("200000000003", "Acme Q9 Signal Tracer boxed", 10000, 1200, true, "New"),
                                            card("200000000004", "Acme Q9 offer", 7000, 1000, false, "Used"),
                                            card("200000000005", "Acme Q9 manual", 1500, 300, true, "")}},
                       {"hasMorePages", false}};
}

QJsonObject activePage()
{
    return QJsonObject{{"items", QJsonArray{card("100000000070", "Acme Q9 Signal Tracer", 12000, 4500, false, "Used"),
                                            card("200000000011", "Acme Q9 Signal Tracer", 9500, 800, false, "Used"),
                                            card("200000000012", "Acme Q9", 11000, 0, false, "For parts or not working"),
                                            card("200000000013", "Tip for Acme Q9", 900, 200, false, "New")}},
                       {"hasMorePages", false}};
}

QJsonObject researchPage()
{
    return QJsonObject{{"rows", QJsonArray{QJsonObject{{"itemId", "300000000001"},
                                                       {"title", "Acme Q9 Signal Tracer"},
                                                       {"avgSoldPriceCents", 8500},
                                                       {"avgShippingCents", 900},
                                                       {"formatText", "Fixed price"},
                                                       {"totalSold", 2},
                                                       {"lastSoldDate", "2026-09-30"}},
                                           QJsonObject{{"itemId", "300000000002"},
                                                       {"title", "Acme Q9"},
                                                       {"avgSoldPriceCents", 9900},
                                                       {"avgShippingCents", 1100},
                                                       {"formatText", "Auction"},
                                                       {"totalSold", 1},
                                                       {"lastSoldDate", "2026-09-12"}}}},
                       {"morePossible", false},
                       {"summary", QJsonObject{{"avgSoldPriceCents", 9100}, {"avgShippingCents", 1000}}}};
}

QList<app::Migration> shippedMigrations()
{
    QString error;
    const auto migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
    return migrations ? *migrations : QList<app::Migration>{};
}

QVariant scalar(const QString &connection, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connection));
    if (!query.exec(sql) || !query.next())
        return {};
    return query.value(0);
}

bool exec(const QString &connection, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connection));
    if (query.exec(sql))
        return true;
    qWarning().noquote() << sql << query.lastError().text();
    return false;
}

} // namespace

class TestLookupStore : public QObject {
    Q_OBJECT

private:
    MatchWords m_words;
    KnownModels m_known;
    QTemporaryDir *m_tmp = nullptr;
    QString m_db;
    QString m_backups;
    // A complete lookup of the item above, run through LookupRunner.
    LookupResult runLookup();

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void roundTrip();
    void checkboxesAndNumbersAreStored();
    void recentList();
    void statusesMapBothWays();
    void identitiesAndPhotos();
    void priceHistory();
    void upgradesVersion1();
};

void TestLookupStore::initTestCase()
{
    QString error;
    const std::optional<MatchWords> words = MatchWords::builtIn(&error);
    QVERIFY2(words, qPrintable(error));
    m_words = *words;
}

void TestLookupStore::init()
{
    m_tmp = new QTemporaryDir;
    QVERIFY(m_tmp->isValid());
    m_db = m_tmp->filePath(QStringLiteral("test.sqlite"));
    m_backups = m_tmp->filePath(QStringLiteral("backups"));
    QVERIFY(QDir().mkpath(m_backups));
}

void TestLookupStore::cleanup()
{
    for (const QString &name : QSqlDatabase::connectionNames())
        Database::close(name);
    delete m_tmp;
    m_tmp = nullptr;
}

LookupResult TestLookupStore::runLookup()
{
    LookupRunner runner([](const QUrl &, PageKind) {}, &m_known, &m_words);
    QString error;
    if (!runner.startItem(QStringLiteral("100000000070"), &error))
        return {};
    runner.onReply(okReply(itemData()));
    if (!runner.startComps(runner.result().queries, Depths{240, 240, 200}, 90, &error))
        return {};
    runner.onReply(okReply(soldPage()));
    runner.onReply(okReply(activePage()));
    runner.onReply(okReply(researchPage()));
    return runner.result();
}

void TestLookupStore::roundTrip()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupResult result = runLookup();
    QCOMPARE(result.status, QStringLiteral("complete"));
    QCOMPARE(result.sold.size(), 5);
    QCOMPARE(result.active.size(), 4);
    QCOMPARE(result.research.size(), 2);
    const QList<Row> rows = classifyRows(result, m_words);
    const app::ValueSettings settings;
    const Valuation valuation = valuate(result, rows, settings);
    // Sold, same item, confirmed totals $90.00, $100.00, $112.00: median
    // $100.00; the Best Offer sale is counted apart, the manual not at all.
    QCOMPARE(valuation.sold.included, 3);
    QCOMPARE(*valuation.sold.medianCents, 10000);
    QCOMPARE(valuation.sold.unconfirmed, 1);

    const LookupStore store(QStringLiteral("main"));
    QString error;
    const std::optional<qint64> id =
        store.save(QStringLiteral("https://www.ebay.com/itm/100000000070"), result, rows, valuation, Depths{240, 240, 200}, 90, &error);
    QVERIFY2(id, qPrintable(error));

    const std::optional<SavedLookup> saved = store.load(*id, &error);
    QVERIFY2(saved, qPrintable(error));
    QCOMPARE(saved->input, QStringLiteral("https://www.ebay.com/itm/100000000070"));
    QVERIFY(saved->createdAt.isValid());
    QCOMPARE(saved->depths.sold, 240);
    QCOMPARE(saved->depths.research, 200);
    QCOMPARE(saved->historyDays, 90);
    const LookupResult &back = saved->result;
    QCOMPARE(back.status, QStringLiteral("complete"));
    QCOMPARE(back.item.itemId, result.item.itemId);
    QCOMPARE(back.item.title, result.item.title);
    QCOMPARE(back.item.imageUrl, QStringLiteral("https://images.example.com/q9.jpg"));
    QCOMPARE(back.item.priceCents, result.item.priceCents);
    QCOMPARE(back.item.shippingCents, result.item.shippingCents);
    QCOMPARE(back.item.condition, QStringLiteral("Used"));
    QCOMPARE(back.item.sellerName, QStringLiteral("examplestore"));
    QCOMPARE(back.item.sellerFeedbackCount, std::optional<int>(7));
    QCOMPARE(back.item.sellerPositivePercent, std::optional<double>(98.5));
    QCOMPARE(back.item.specifics.size(), 2);
    QCOMPARE(back.item.specific(QStringLiteral("Model")), QStringLiteral("Q9"));
    QCOMPARE(back.identity.brandName, result.identity.brandName);
    QCOMPARE(back.identity.model, result.identity.model);
    QCOMPARE(back.identity.source, result.identity.source);
    QCOMPARE(back.identity.confident, result.identity.confident);
    QCOMPARE(back.queries, result.queries);
    QCOMPARE(back.notes, result.notes);
    QCOMPARE(back.researchSummary, result.researchSummary);
    QCOMPARE(back.pagesLoaded, result.pagesLoaded);

    // Every row, with its bucket, reasons and checkbox, in the same order.
    QCOMPARE(saved->rows.size(), rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QCOMPARE(saved->rows.at(i).source, rows.at(i).source);
        QCOMPARE(saved->rows.at(i).index, rows.at(i).index);
        QCOMPARE(saved->rows.at(i).match.bucket, rows.at(i).match.bucket);
        QCOMPARE(saved->rows.at(i).match.reasons, rows.at(i).match.reasons);
        QCOMPARE(saved->rows.at(i).included, rows.at(i).included);
        QCOMPARE(rowTotal(back, saved->rows.at(i)), rowTotal(result, rows.at(i)));
        QCOMPARE(rowCondition(back, saved->rows.at(i)), rowCondition(result, rows.at(i)));
        QCOMPARE(rowTitle(back, saved->rows.at(i)), rowTitle(result, rows.at(i)));
    }
    for (qsizetype i = 0; i < result.sold.size(); ++i) {
        QCOMPARE(back.sold.at(i).confirmed, result.sold.at(i).confirmed);
        QCOMPARE(back.sold.at(i).soldDate, result.sold.at(i).soldDate);
        QCOMPARE(back.sold.at(i).sellerName, result.sold.at(i).sellerName);
        QCOMPARE(back.sold.at(i).format, result.sold.at(i).format);
    }
    QCOMPARE(back.research.at(1).totalSold, result.research.at(1).totalSold);
    QCOMPARE(back.research.at(1).lastSoldDate, result.research.at(1).lastSoldDate);
    QCOMPARE(back.research.at(1).formatText, result.research.at(1).formatText);

    // The numbers from the saved lookup are the numbers of the lookup.
    const Valuation again = valuate(back, saved->rows, settings);
    QCOMPARE(again.sold.medianCents, valuation.sold.medianCents);
    QCOMPARE(again.sold.p25Cents, valuation.sold.p25Cents);
    QCOMPARE(again.sold.p75Cents, valuation.sold.p75Cents);
    QCOMPARE(again.research.medianCents, valuation.research.medianCents);
    QCOMPARE(again.forSale, valuation.forSale);
    QCOMPARE(again.cheaperForSale, valuation.cheaperForSale);
    QCOMPARE(again.shipping.flagged, valuation.shipping.flagged);
    QCOMPARE(again.ebayAverageSoldCents, valuation.ebayAverageSoldCents);

    QVERIFY(!store.load(*id + 1, &error));
}

void TestLookupStore::checkboxesAndNumbersAreStored()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupResult result = runLookup();
    QList<Row> rows = classifyRows(result, m_words);
    const app::ValueSettings settings;
    const LookupStore store(QStringLiteral("main"));
    QString error;
    const std::optional<qint64> id = store.save(QStringLiteral("100000000070"), result, rows, valuate(result, rows, settings),
                                                Depths{240, 240, 200}, 90, &error);
    QVERIFY2(id, qPrintable(error));

    // Untick the cheapest same-item sale ($90.00) and tick nothing else.
    qsizetype cheapest = -1;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows.at(i).source == Row::Source::Sold && rows.at(i).included && rowTotal(result, rows.at(i)) == 9000)
            cheapest = i;
    }
    QVERIFY(cheapest >= 0);
    rows[cheapest].included = false;
    const Valuation changed = valuate(result, rows, settings);
    QCOMPARE(changed.sold.included, 2);
    QVERIFY2(store.updateIncluded(*id, result, rows, changed, &error), qPrintable(error));

    const std::optional<SavedLookup> saved = store.load(*id, &error);
    QVERIFY2(saved, qPrintable(error));
    QCOMPARE(saved->rows.at(cheapest).included, false);
    QCOMPARE(valuate(saved->result, saved->rows, settings).sold.medianCents, changed.sold.medianCents);
    const std::optional<QList<LookupSummary>> recent = store.recent(10, &error);
    QVERIFY2(recent, qPrintable(error));
    QCOMPARE(recent->size(), 1);
    QCOMPARE(recent->first().soldCount, 2);
    QCOMPARE(recent->first().soldMedianCents, changed.sold.medianCents);

    // A lookup that is not there is refused, and nothing is changed.
    QVERIFY(!store.updateIncluded(*id + 1, result, rows, changed, &error));
}

void TestLookupStore::recentList()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupResult result = runLookup();
    const QList<Row> rows = classifyRows(result, m_words);
    const Valuation valuation = valuate(result, rows, app::ValueSettings());
    const LookupStore store(QStringLiteral("main"));
    QString error;
    const std::optional<qint64> first = store.save(QStringLiteral("a"), result, rows, valuation, Depths{1, 1, 1}, 90, &error);
    const std::optional<qint64> second = store.save(QStringLiteral("b"), result, rows, valuation, Depths{1, 1, 1}, 90, &error);
    QVERIFY(first && second);
    const std::optional<QList<LookupSummary>> recent = store.recent(10, &error);
    QVERIFY2(recent, qPrintable(error));
    QCOMPARE(recent->size(), 2);
    // Newest first.
    QCOMPARE(recent->at(0).id, *second);
    QCOMPARE(recent->at(1).id, *first);
    const LookupSummary &summary = recent->first();
    QCOMPARE(summary.itemId, QStringLiteral("100000000070"));
    QCOMPARE(summary.brand, result.identity.brandName);
    QCOMPARE(summary.model, result.identity.model);
    QCOMPARE(summary.status, QStringLiteral("complete"));
    QCOMPARE(summary.listingTotalCents, std::optional<qint64>(16500));
    QCOMPARE(summary.soldCount, valuation.sold.included);
    QCOMPARE(summary.soldConfidence, valuation.sold.confidence);
    QCOMPARE(summary.shippingFlagged, valuation.shipping.flagged);
    QCOMPARE(summary.forSaleCount, static_cast<int>(valuation.forSale.size()));
    QCOMPARE(summary.cheaperForSale, valuation.cheaperForSale);
    QCOMPARE(summary.cheapestForSaleCents, std::optional<qint64>(valuation.forSale.first()));
    if (valuation.shipping.flagged)
        QVERIFY(summary.shippingFlagRatio.has_value());
    QCOMPARE(store.recent(1, &error)->size(), 1);
}

void TestLookupStore::statusesMapBothWays()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupStore store(QStringLiteral("main"));
    for (const char *status : {"complete", "cancelled", "stopped"}) {
        LookupResult result;
        result.status = QString::fromLatin1(status);
        result.message = QStringLiteral("what happened");
        QString error;
        const std::optional<qint64> id = store.save(QStringLiteral("x"), result, {}, Valuation(), Depths(), 90, &error);
        QVERIFY2(id, qPrintable(error));
        QCOMPARE(store.load(*id, &error)->result.status, QString::fromLatin1(status));
        QCOMPARE(store.load(*id, &error)->result.message, QStringLiteral("what happened"));
    }
    QCOMPARE(scalar(QStringLiteral("main"), QStringLiteral("SELECT group_concat(status, ',') FROM lookups")).toString(),
             QStringLiteral("complete,partial,failed"));
}

void TestLookupStore::identitiesAndPhotos()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupStore store(QStringLiteral("main"));
    QString error;
    QVERIFY2(store.saveUserIdentity(QStringLiteral("100000000070"), {QStringLiteral(" Acme "), QStringLiteral("Q9"), QString()},
                                    std::nullopt, &error),
             qPrintable(error));
    QVERIFY2(store.saveUserIdentity(QStringLiteral("100000000070"), {QStringLiteral("Acme"), QStringLiteral("Q9B"),
                                                                     QStringLiteral("Signal Tracer")},
                                    std::nullopt, &error),
             qPrintable(error));
    const std::optional<QList<ConfirmedIdentity>> identities = store.userIdentities(QStringLiteral("100000000070"), &error);
    QVERIFY2(identities, qPrintable(error));
    QCOMPARE(identities->size(), 2);
    QCOMPARE(identities->at(0).brand, QStringLiteral("Acme"));
    QCOMPARE(identities->at(1).model, QStringLiteral("Q9B"));
    QCOMPARE(identities->at(1).productName, QStringLiteral("Signal Tracer"));
    QCOMPARE(store.userIdentities(QStringLiteral("100000000071"), &error)->size(), 0);

    QVERIFY(store.photo(QStringLiteral("100000000070")).isEmpty());
    QVERIFY2(store.savePhoto(QStringLiteral("100000000070"), QStringLiteral("https://images.example.com/a.jpg"), "first", &error),
             qPrintable(error));
    QVERIFY2(store.savePhoto(QStringLiteral("100000000070"), QStringLiteral("https://images.example.com/b.jpg"), "second", &error),
             qPrintable(error));
    QCOMPARE(store.photo(QStringLiteral("100000000070")), QByteArray("second"));
    QCOMPARE(scalar(QStringLiteral("main"), QStringLiteral("SELECT count(*) FROM listing_photos")).toInt(), 1);
}

// A version-1 database with one lookup of every kind of row becomes version
// 2: backed up first, the rows copied as described in 0002_saved_lookups.sql,
// the version-1 tables gone.
void TestLookupStore::priceHistory()
{
    QVERIFY(Database::open(QStringLiteral("main"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), shippedMigrations()).ok());
    const LookupResult result = runLookup();
    QList<Row> rows = classifyRows(result, m_words);
    const Valuation valuation = valuate(result, rows, app::ValueSettings());
    const LookupStore store(QStringLiteral("main"));
    QString error;
    const std::optional<qint64> older = store.save(QStringLiteral("a"), result, rows, valuation, Depths{1, 1, 1}, 90, &error);
    const std::optional<qint64> newer = store.save(QStringLiteral("b"), result, rows, valuation, Depths{1, 1, 1}, 90, &error);
    QVERIFY2(older && newer, qPrintable(error));
    QCOMPARE(store.recent(0, &error)->first().input, QStringLiteral("b"));

    // Both lookups are one item.
    const std::optional<QList<LookupSummary>> all = store.recent(0, &error);
    const QList<HistoryItem> items = historyItems(*all, m_known);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().checks.size(), 2);
    QCOMPARE(items.first().checks.first().id, *newer);

    // Sale rows: the confirmed sold rows and the one-sale research row, newest
    // lookup first.
    std::optional<QList<HistorySaleRow>> sales = store.historySales({*newer, *older}, &error);
    QVERIFY2(sales, qPrintable(error));
    QCOMPARE(sales->size(), 10); // 4 confirmed sold + 1 research, twice
    QCOMPARE(sales->first().lookupId, *newer);
    // Each listing once; the manual is not counted; totals incl. shipping.
    HistoryPoints points = historyPoints(*sales, QDate(2026, 10, 20), 90);
    QCOMPARE(points.sales.size(), 4);
    QCOMPARE(points.sales.first().soldOn, QDate(2026, 9, 12));
    QCOMPARE(points.sales.first().totalCents, 11000);
    QCOMPARE(points.medianCents, std::optional<qint64>(10500));
    QCOMPARE(points.p25Cents, std::optional<qint64>(9750));
    QCOMPARE(points.p75Cents, std::optional<qint64>(11050));
    // A shorter window leaves the September sale out.
    QCOMPARE(historyPoints(*sales, QDate(2026, 10, 20), 30).sales.size(), 3);

    // The newest lookup's ticks decide: unticking a sale there removes it.
    rows[0].included = false;
    QVERIFY2(store.updateIncluded(*newer, result, rows, valuate(result, rows, app::ValueSettings()), &error), qPrintable(error));
    sales = store.historySales({*newer, *older}, &error);
    points = historyPoints(*sales, QDate(2026, 10, 20), 90);
    QCOMPARE(points.sales.size(), 3);
    QVERIFY(std::none_of(points.sales.cbegin(), points.sales.cend(),
                         [](const HistorySale &sale) { return sale.itemId == QLatin1String("200000000001"); }));

    // One item under any brand name or family member (the built-in data).
    const std::optional<KnownModels> builtIn = KnownModels::builtIn(&error);
    QVERIFY2(builtIn, qPrintable(error));
    QCOMPARE(historyKey(QStringLiteral("HP"), QStringLiteral("8904A"), *builtIn),
             historyKey(QStringLiteral("Hewlett Packard"), QStringLiteral("8904a"), *builtIn));
    QCOMPARE(historyKey(QStringLiteral("Marconi"), QStringLiteral("2022D"), *builtIn),
             historyKey(QStringLiteral("Marconi"), QStringLiteral("2022"), *builtIn));
    QVERIFY(historyKey(QStringLiteral("Sencore"), QStringLiteral("PR57"), *builtIn)
            != historyKey(QStringLiteral("Sencore"), QStringLiteral("PR570"), *builtIn));
}

void TestLookupStore::upgradesVersion1()
{
    const QList<app::Migration> all = shippedMigrations();
    QCOMPARE(all.size(), 4);
    const Database::Result created =
        Database::open(QStringLiteral("v1"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), all.mid(0, 1));
    QVERIFY2(created.ok(), qPrintable(created.error));
    QCOMPARE(created.toVersion, 1);
    const QString v1 = QStringLiteral("v1");
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO lookup (id, created_at, input, item_id, query, marketplace, status, error, "
                                    "app_version) VALUES (7, '2026-10-01T12:00:00.000Z', '100000000080', '100000000080', "
                                    "'Acme Q9', 'EBAY_US', 'partial', 'cancelled by the user', '0.0.9')")));
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO listing (id, lookup_id, role, source, item_id, title, condition, format, "
                                    "price_cents, currency, shipping_cents, price_status) VALUES "
                                    "(1, 7, 'subject', 'item_page', '100000000080', 'Acme Q9 Tracer', 'Used', 'fixed', "
                                    "12000, 'USD', 1500, 'asking')")));
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO listing (id, lookup_id, role, source, item_id, title, condition, format, "
                                    "price_cents, currency, shipping_cents, bids, ended_at, price_status) VALUES "
                                    "(2, 7, 'sold', 'sold_search', '200000000021', 'Acme Q9 sold', 'Used', 'auction', "
                                    "9000, 'USD', 1000, 4, '2026-09-20T18:00:00Z', 'confirmed'), "
                                    "(3, 7, 'sold', 'sold_search', '200000000022', 'Acme Q9 offer', 'Used', 'best_offer', "
                                    "8000, 'USD', 1000, NULL, '2026-09-21T18:00:00Z', 'unconfirmed'), "
                                    "(4, 7, 'active', 'active_search', '200000000023', 'Acme Q9 for sale', 'New', "
                                    "'unknown', 11000, 'USD', 0, NULL, NULL, 'asking'), "
                                    "(5, 7, 'sold', 'product_research', '300000000021', 'Acme Q9 research', NULL, NULL, "
                                    "8800, 'USD', 700, NULL, '2026-09-02T00:00:00Z', 'confirmed')")));
    QVERIFY(exec(v1, QStringLiteral("UPDATE listing SET quantity_sold = 3, total_sales_cents = 26400 WHERE id = 5")));
    // A sale priced in another currency: never read as US cents.
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO listing (id, lookup_id, role, source, item_id, title, price_cents, "
                                    "currency, shipping_cents, ended_at, price_status) VALUES (6, 7, 'sold', "
                                    "'sold_search', '200000000024', 'Acme Q9 abroad', 7700, 'EUR', 900, "
                                    "'2026-09-22T18:00:00Z', 'confirmed')")));
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO excluded_listing (listing_id, reason) VALUES (3, 'unconfirmed_not_used')")));
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO research_summary (lookup_id, avg_sold_cents, min_sold_cents, max_sold_cents, "
                                    "free_shipping_pct, avg_shipping_cents, sellers_count, currency) VALUES "
                                    "(7, 9100, 7000, 12000, 25.5, 800, 6, 'USD')")));
    QVERIFY(exec(v1, QStringLiteral("INSERT INTO valuation (lookup_id, algo_version, sample_total, sample_kept, basis, "
                                    "value_cents, low_cents, high_cents, typical_shipping_cents, subject_shipping_cents, shipping_verdict, "
                                    "confidence, currency) VALUES "
                                    "(7, 1, 3, 2, 'confirmed', 9500, 9000, 10000, 1000, 1500, 'above', 'low', 'USD')")));
    Database::close(v1);

    const Database::Result upgraded =
        Database::open(QStringLiteral("v2"), m_db, m_backups, QStringLiteral(TEST_APP_NAME), all);
    QVERIFY2(upgraded.ok(), qPrintable(upgraded.error));
    QCOMPARE(upgraded.outcome, Database::Outcome::Upgraded);
    QCOMPARE(upgraded.fromVersion, 1);
    QCOMPARE(upgraded.toVersion, 4);
    QVERIFY(QFileInfo::exists(upgraded.backupPath));
    QCOMPARE(scalar(QStringLiteral("v2"), QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
    for (const char *table : {"lookup", "listing", "research_summary", "valuation", "excluded_listing"}) {
        QCOMPARE(scalar(QStringLiteral("v2"), QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = '%1'")
                                                  .arg(QLatin1String(table)))
                     .toInt(),
                 0);
    }

    const LookupStore store(QStringLiteral("v2"));
    QString error;
    const std::optional<SavedLookup> saved = store.load(7, &error);
    QVERIFY2(saved, qPrintable(error));
    const LookupResult &result = saved->result;
    QCOMPARE(saved->input, QStringLiteral("100000000080"));
    QCOMPARE(saved->createdAt, QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC));
    QCOMPARE(result.status, QStringLiteral("cancelled"));
    QCOMPARE(result.message, QStringLiteral("cancelled by the user"));
    QCOMPARE(result.item.itemId, QStringLiteral("100000000080"));
    QCOMPARE(result.item.title, QStringLiteral("Acme Q9 Tracer"));
    QCOMPARE(result.item.priceCents, std::optional<qint64>(12000));
    QCOMPARE(result.item.shippingCents, std::optional<qint64>(1500));
    QCOMPARE(result.item.condition, QStringLiteral("Used"));
    QCOMPARE(result.queries, QStringList{QStringLiteral("Acme Q9")});
    QCOMPARE(result.identity.source, QStringLiteral("older version"));
    QCOMPARE(result.identity.confident, false);
    QCOMPARE(result.researchSummary.value("avgSoldPriceCents").toInt(), 9100);
    QCOMPARE(result.researchSummary.value("avgShippingCents").toInt(), 800);
    QCOMPARE(result.researchSummary.value("totalSellers").toInt(), 6);
    QCOMPARE(result.researchSummary.value("freeShippingPercent").toDouble(), 25.5);
    QCOMPARE(result.researchSummary.value("soldPriceRange").toObject().value("minCents").toInt(), 7000);
    QCOMPARE(result.researchSummary.value("soldPriceRange").toObject().value("maxCents").toInt(), 12000);

    QCOMPARE(result.notes, (QStringList{QStringLiteral("version 1 value math (algorithm 1): basis confirmed, 2 of 3 sales kept"),
                                        QStringLiteral("version 1 shipping check: above (listing $15.00, typical $10.00)")}));
    QCOMPARE(result.sold.size(), 3);
    QCOMPARE(result.active.size(), 1);
    QCOMPARE(result.research.size(), 1);
    QCOMPARE(result.sold.at(0).format, QStringLiteral("auction"));
    QCOMPARE(result.sold.at(0).bids, std::optional<int>(4));
    QCOMPARE(result.sold.at(0).soldDate, QStringLiteral("2026-09-20"));
    QCOMPARE(result.sold.at(0).confirmed, true);
    QCOMPARE(result.sold.at(1).format, QStringLiteral("fixed_price"));
    QCOMPARE(result.sold.at(1).confirmed, false);
    QCOMPARE(result.sold.at(2).priceCents, std::optional<qint64>());
    QCOMPARE(result.sold.at(2).shippingCents, std::optional<qint64>());
    QCOMPARE(result.active.at(0).format, QString());
    QCOMPARE(result.active.at(0).soldDate, QString());
    QCOMPARE(result.research.at(0).avgSoldPriceCents, std::optional<qint64>(8800));
    QCOMPARE(result.research.at(0).avgShippingCents, std::optional<qint64>(700));
    QCOMPARE(result.research.at(0).totalSold, std::optional<int>(3));
    QCOMPARE(result.research.at(0).lastSoldDate, QStringLiteral("2026-09-02"));
    QCOMPARE(saved->rows.size(), 5);
    const QString notMatched = QStringLiteral("saved by an older version of the app: not matched");
    for (const Row &row : saved->rows) {
        QCOMPARE(row.match.bucket, Bucket::ToCheck);
        QCOMPARE(row.match.reasons.first(), notMatched);
        const bool excluded = row.source == Row::Source::Sold && row.index == 1;
        const bool foreign = row.source == Row::Source::Sold && row.index == 2;
        QCOMPARE(row.included, !excluded && !foreign);
        if (excluded)
            QCOMPARE(row.match.reasons, (QStringList{notMatched, QStringLiteral("version 1 left it out: unconfirmed_not_used")}));
        else if (foreign)
            QCOMPARE(row.match.reasons, (QStringList{notMatched, QStringLiteral("price in EUR: 7700 + 900 shipping (hundredths)")}));
        else if (row.source == Row::Source::Research)
            QCOMPARE(row.match.reasons, (QStringList{notMatched, QStringLiteral("version 1 total sales: 26400 hundredths")}));
        else
            QCOMPARE(row.match.reasons, QStringList{notMatched});
    }

    // The card numbers as version 1 had them.
    const std::optional<QList<LookupSummary>> copied = store.recent(10, &error);
    QVERIFY2(copied, qPrintable(error));
    QCOMPARE(copied->first().soldCount, 2);
    QCOMPARE(copied->first().soldMedianCents, std::optional<qint64>(9500));
    QCOMPARE(copied->first().soldConfidence, QStringLiteral("Low"));

    // The user's checkbox on a migrated lookup is saved (positions 0, 1, ...
    // per source, as load() numbers them).
    QList<Row> rows = saved->rows;
    rows[0].included = false;
    const Valuation changed = valuate(result, rows, app::ValueSettings());
    QVERIFY2(store.updateIncluded(7, result, rows, changed, &error), qPrintable(error));
    QCOMPARE(store.load(7, &error)->rows.at(0).included, false);
    rows[0].included = true;
    QVERIFY2(store.updateIncluded(7, result, rows, valuate(result, rows, app::ValueSettings()), &error), qPrintable(error));

    const std::optional<QList<LookupSummary>> recent = store.recent(10, &error);
    QVERIFY2(recent, qPrintable(error));
    QCOMPARE(recent->size(), 1);
    QCOMPARE(recent->first().status, QStringLiteral("partial"));
    QCOMPARE(recent->first().listingTotalCents, std::optional<qint64>(13500));
    // Recomputed from the rows when the checkboxes were saved: one counted
    // US-dollar sale ($90.00 + $10.00).
    QCOMPARE(recent->first().soldCount, 1);
    QCOMPARE(recent->first().soldMedianCents, std::optional<qint64>(10000));
    QCOMPARE(recent->first().soldConfidence, QStringLiteral("Not enough data"));

    // The backup is the version-1 file.
    QSqlDatabase backup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("backup"));
    backup.setDatabaseName(upgraded.backupPath);
    QVERIFY(backup.open());
    QCOMPARE(scalar(QStringLiteral("backup"), QStringLiteral("PRAGMA user_version")).toInt(), 1);
    QCOMPARE(scalar(QStringLiteral("backup"), QStringLiteral("SELECT count(*) FROM listing")).toInt(), 6);
    backup.close();
}

QTEST_GUILESS_MAIN(TestLookupStore)
#include "tst_lookupstore.moc"
