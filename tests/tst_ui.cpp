// The Qt Quick user interface with synthetic data, offscreen (no display):
// every screen and panel loads and renders without a QML warning; a saved
// lookup reopens from the database (nothing loaded from eBay) with its
// numbers; ticking a row recomputes them and stores the choice; the fonts
// are bundled. Every item, seller and number is invented.
#include "core/Settings.h"
#include "core/WorkFolder.h"
#include "db/Database.h"
#include "db/Migrations.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupStore.h"
#include "ui/Backend.h"
#include "ui/Fonts.h"
#include "ui/PhotoProvider.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QMap>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <memory>

Q_IMPORT_QML_PLUGIN(AppUiPlugin)

using namespace app::lookup;

namespace {

const char kModule[] = "GreedbayDetective.Ui";

// Warnings and errors of the test (QML and others); the offscreen platform's
// note that it cannot size windows is not a problem of the views.
QStringList g_warnings;

void collectWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtDebugMsg || type == QtInfoMsg)
        return;
    if (message.contains(QLatin1String("This plugin does not support")))
        return;
    g_warnings.append(message);
    // Also shown at once, so a failure's cause is in the test output.
    fprintf(stderr, "WARNING: %s\n", qPrintable(message));
}

SearchRecord sale(const QString &id, const QString &title, qint64 price, qint64 shipping, const QString &condition)
{
    SearchRecord record;
    record.itemId = id;
    record.title = title;
    record.priceCents = price;
    record.shippingCents = shipping;
    record.condition = condition;
    record.soldDate = QStringLiteral("2026-10-0%1").arg(id.right(1));
    record.confirmed = true;
    record.format = QStringLiteral("fixed_price");
    return record;
}

Row row(Row::Source source, qsizetype index, Bucket bucket, const QString &reason = {})
{
    Row r;
    r.source = source;
    r.index = index;
    r.match.bucket = bucket;
    if (!reason.isEmpty())
        r.match.reasons = {reason};
    r.included = bucket == Bucket::SameItem;
    return r;
}

} // namespace

class TestUi : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<app::WorkFolder> m_folder;
    qint64 m_lookupId = 0;
    // Renders `window` once, so every visible delegate is created.
    static bool render(QQuickWindow *window);
    bool loadItem(const char *type, const QVariantMap &properties);

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void fontsAreBundled();
    void everyScreenLoads();
    void savedLookupReopens();
    void panelsLoad();
    void nothingFoundIsSaid();
    void auctionsAndFormats();
};

void TestUi::initTestCase()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    QString error;
    m_folder = app::WorkFolder::open(m_tmp->filePath(QStringLiteral("work")), QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(m_folder, qPrintable(error));
    const auto migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
    QVERIFY2(migrations, qPrintable(error));
    const app::DatabaseResult database = app::Database::open(QStringLiteral("app"), m_folder->databasePath(),
                                                             m_folder->backupsDir(), QStringLiteral(TEST_APP_NAME), *migrations);
    QVERIFY2(database.ok(), qPrintable(database.error));

    // One saved lookup: the pasted listing ($120 + $45 shipping), four
    // same-item sales, a manual, the listing itself and two cheaper
    // listings for sale, one Product Research row.
    LookupResult result;
    result.status = QStringLiteral("complete");
    result.item.itemId = QStringLiteral("100000000090");
    result.item.title = QStringLiteral("Acme Q9 Signal Tracer");
    result.item.priceCents = 12000;
    result.item.shippingCents = 4500;
    result.item.currency = QStringLiteral("USD");
    result.item.condition = QStringLiteral("Used");
    result.item.sellerName = QStringLiteral("examplestore");
    result.identity.brandName = QStringLiteral("Acme");
    result.identity.model = QStringLiteral("Q9");
    result.identity.source = QStringLiteral("item specifics");
    result.identity.confident = true;
    result.identity.notes = {QStringLiteral("an invented known issue")};
    result.queries = {QStringLiteral("Acme Q9")};
    result.sold = {sale("200000000001", "Acme Q9 Signal Tracer", 8000, 1000, "Used"),
                   sale("200000000002", "Acme Q9 tracer", 9000, 1000, "Used"),
                   sale("200000000003", "Acme Q9", 10000, 1200, "For parts or not working"),
                   sale("200000000004", "Acme Q9 boxed", 11000, 1000, "New"),
                   sale("200000000005", "Acme Q9 manual", 1500, 300, "")};
    result.active = {sale("100000000090", "Acme Q9 Signal Tracer", 12000, 4500, "Used"),
                     sale("200000000011", "Acme Q9", 9500, 800, "Used"),
                     sale("200000000012", "Acme Q9 tracer", 11000, 0, "Used")};
    ResearchRecord research;
    research.itemId = QStringLiteral("300000000001");
    research.title = QStringLiteral("Acme Q9");
    research.avgSoldPriceCents = 8500;
    research.avgShippingCents = 900;
    research.totalSold = 2;
    research.lastSoldDate = QStringLiteral("2026-09-30");
    result.research = {research};
    result.researchSummary = QJsonObject{{"avgSoldPriceCents", 9100}, {"avgShippingCents", 1000}};
    result.pagesLoaded = 4;
    QList<Row> rows = {row(Row::Source::Sold, 0, Bucket::SameItem),     row(Row::Source::Sold, 1, Bucket::SameItem),
                       row(Row::Source::Sold, 2, Bucket::SameItem),     row(Row::Source::Sold, 3, Bucket::SameItem),
                       row(Row::Source::Sold, 4, Bucket::Documentation, QStringLiteral("documentation word \"manual\"")),
                       row(Row::Source::Active, 0, Bucket::SameItem),   row(Row::Source::Active, 1, Bucket::SameItem),
                       row(Row::Source::Active, 2, Bucket::SameItem),   row(Row::Source::Research, 0, Bucket::SameItem)};
    const LookupStore store(QStringLiteral("app"));
    const Valuation valuation = valuate(result, rows, app::ValueSettings());
    const std::optional<qint64> id = store.save(QStringLiteral("100000000090"), result, rows, valuation,
                                                app::lookup::Depths{240, 240, 200},
                                                app::Settings::kDefaultSoldHistoryDays, &error);
    QVERIFY2(id, qPrintable(error));
    m_lookupId = *id;

    // A photo for the listing (a synthetic 8x8 image).
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::darkGray);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPG"));
    QVERIFY2(store.savePhoto(result.item.itemId, QStringLiteral("https://images.example.com/q9.jpg"), jpeg, &error),
             qPrintable(error));

    qInstallMessageHandler(collectWarnings);
}

void TestUi::cleanupTestCase()
{
    qInstallMessageHandler(nullptr);
    app::Database::close(QStringLiteral("app"));
}

void TestUi::init()
{
    g_warnings.clear();
}

bool TestUi::render(QQuickWindow *window)
{
    window->show();
    if (!QTest::qWaitForWindowExposed(window))
        return false;
    QCoreApplication::processEvents();
    return !window->grabWindow().isNull();
}

void TestUi::fontsAreBundled()
{
    QString error;
    QVERIFY2(app::ui::loadFonts(&error), qPrintable(error));
    const QStringList families = QFontDatabase::families();
    QVERIFY(families.contains(QStringLiteral("IBM Plex Sans")));
    QVERIFY(families.contains(QStringLiteral("IBM Plex Mono")));
    QCOMPARE(QGuiApplication::font().families().value(0), QStringLiteral("IBM Plex Sans"));
}

void TestUi::everyScreenLoads()
{
    app::ui::Backend backend(*m_folder, QStringLiteral("app"));
    QVERIFY2(backend.fatalError().isEmpty(), qPrintable(backend.fatalError()));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("listing"), backend.takePhotoProvider());
    engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)}});
    engine.loadFromModule(kModule, "Main");
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    QVERIFY(render(window));
    // The recent lookups list the saved one.
    QCOMPARE(backend.recent()->count(), 1);
    // Home, Price history, Settings, then the saved lookup on Results.
    for (const int screen : {0, 2, 3, 0}) {
        QVERIFY(window->setProperty("screen", screen));
        QVERIFY(render(window));
    }
    QVERIFY(backend.openSaved(m_lookupId));
    QCOMPARE(window->property("screen").toInt(), 1);
    QVERIFY(render(window));
    QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QLatin1Char('\n'))));
}

void TestUi::savedLookupReopens()
{
    app::ui::Backend backend(*m_folder, QStringLiteral("app"));
    app::ui::PhotoProvider *photos = backend.takePhotoProvider();
    std::unique_ptr<app::ui::PhotoProvider> ownPhotos(photos);
    QVERIFY(backend.openSaved(m_lookupId));
    app::ui::ResultsController *results = backend.results();
    QVERIFY(results->hasLookup());
    QCOMPARE(results->itemId(), QStringLiteral("100000000090"));
    QCOMPARE(results->identityName(), QStringLiteral("Acme Q9"));
    QCOMPARE(results->identitySource(), QStringLiteral("from item specifics"));
    QCOMPARE(results->totalText(), QStringLiteral("$165.00"));
    // The saved photo is shown from memory.
    QVERIFY(results->photoSource().startsWith(QStringLiteral("image://listing/100000000090?")));
    QSize size;
    QVERIFY(!photos->requestImage(QStringLiteral("100000000090?1"), &size, QSize()).isNull());
    QCOMPARE(size, QSize(8, 8));
    // Sold same item: $90, $100, $112, $120: median $106.00, P25 $97.50,
    // P75 $114.00 (linear interpolation, PERCENTILE.INC).
    QCOMPARE(results->medianText(), QStringLiteral("$106.00"));
    QCOMPARE(results->middleText(), QStringLiteral("$97.50 - $114.00"));
    QCOMPARE(results->lowHighText(), QStringLiteral("$90.00 / $120.00"));
    // For sale: the listing itself is not counted; $103.00 and $110.00.
    QCOMPARE(results->forSaleText(), QStringLiteral("2, from $103.00"));
    QVERIFY(results->cheaperFlag());
    QCOMPARE(results->cheaperLine(), QStringLiteral("2 listings of the same item cost less: $103.00 total"));
    QCOMPARE(results->cheapestItemId(), QStringLiteral("200000000011"));
    // Shipping $45 against the matched sales' $10.00 median and eBay's $10.00.
    QVERIFY(results->shippingFlag());
    QCOMPARE(results->shippingLines().first(), QStringLiteral("$45.00 is 4.5x eBay's average for this item ($10.00)"));
    QCOMPARE(results->sameItem()->count(), 8);
    QCOMPARE(results->notThisItem()->count(), 1);
    // The same-item table is sorted by total: $90.00 first; the listing
    // itself is marked THIS LISTING, the two cheaper ones CHEAPEST.
    const QModelIndex first = results->sameItem()->index(0);
    QCOMPARE(first.data(app::ui::RowsModel::TotalRole).toString(), QStringLiteral("$90.00"));
    QStringList marks;
    for (int i = 0; i < results->sameItem()->count(); ++i)
        marks.append(results->sameItem()->index(i).data(app::ui::RowsModel::MarkRole).toString());
    QCOMPARE(marks.count(QStringLiteral("this")), 1);
    QCOMPARE(marks.count(QStringLiteral("cheapest")), 2);
    QVERIFY(results->notThisItem()->index(0).data(app::ui::RowsModel::ReasonRole).toString().contains(QLatin1String("manual")));
    // Likely broken (owner decision 2026-10-19): the "For parts or not
    // working" sale carries the badge and stays counted; the listing itself
    // ("Used", a clean title) has no flag; the numbers above already include
    // that sale ($106.00 median).
    QVERIFY(!results->brokenFlag());
    int broken = 0;
    for (int i = 0; i < results->sameItem()->count(); ++i) {
        const QModelIndex index = results->sameItem()->index(i);
        if (!index.data(app::ui::RowsModel::BrokenRole).toString().isEmpty()) {
            ++broken;
            QCOMPARE(index.data(app::ui::RowsModel::BrokenRole).toString(), QStringLiteral("eBay says 'For parts or not working'"));
            QVERIFY(index.data(app::ui::RowsModel::IncludedRole).toBool());
        }
    }
    QCOMPARE(broken, 1);

    // Unticking the $90.00 sale: median of $100, $112, $120 = $112.00, at once
    // and in the database.
    const int saleRow = first.data(app::ui::RowsModel::RowRole).toInt();
    results->setIncluded(saleRow, false);
    QCOMPARE(results->medianText(), QStringLiteral("$112.00"));
    QVERIFY2(results->storeError().isEmpty(), qPrintable(results->storeError()));
    QCOMPARE(results->sameItem()->index(0).data(app::ui::RowsModel::IncludedRole).toBool(), false);
    QString error;
    const std::optional<SavedLookup> saved = LookupStore(QStringLiteral("app")).load(m_lookupId, &error);
    QVERIFY2(saved, qPrintable(error));
    QCOMPARE(saved->rows.at(saleRow).included, false);
    // Back as it was, for the other tests.
    results->setIncluded(saleRow, true);
    QCOMPARE(results->medianText(), QStringLiteral("$106.00"));

    // Product Research with its period (3 years by default): one listing,
    // $85.00 + $9.00.
    QCOMPARE(results->researchLine(),
             QStringLiteral("Product Research, last 3 years (one value per listing: average sold + average shipping): "
                            "1 sale - not enough data confidence, median $94.00, middle half $94.00 - $94.00"));
    QVERIFY(results->hasResearchNumbers());
    QCOMPARE(results->researchMedianText(), QStringLiteral("$94.00"));
    QCOMPARE(results->barSource(), QStringLiteral("bar: sold search"));
    // With no sold sale counted, the bar shows Product Research.
    QList<int> soldRows;
    for (int i = 0; i < results->sameItem()->count(); ++i) {
        const QModelIndex index = results->sameItem()->index(i);
        if (index.data(app::ui::RowsModel::WhereRole).toString() == QLatin1String("sold"))
            soldRows.append(index.data(app::ui::RowsModel::RowRole).toInt());
    }
    QCOMPARE(soldRows.size(), 4);
    for (const int r : soldRows)
        results->setIncluded(r, false);
    QVERIFY(!results->hasSoldNumbers());
    QCOMPARE(results->barSource(),
             QStringLiteral("bar: Product Research, last 3 years (the sold search counted no sale)"));
    QCOMPARE(results->medianValue(), 9400.0);
    for (const int r : soldRows)
        results->setIncluded(r, true);
    QCOMPARE(results->medianText(), QStringLiteral("$106.00"));
}

bool TestUi::loadItem(const char *type, const QVariantMap &properties)
{
    // Destroyed in reverse: the item, then its window, then the engine.
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(700, 700);
    QQmlComponent component(&engine);
    component.loadFromModule(kModule, type);
    if (!component.isReady()) {
        qWarning().noquote() << component.errorString();
        return false;
    }
    std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
    auto *item = qobject_cast<QQuickItem *>(object.get());
    if (!item)
        return false;
    item->setParentItem(window.contentItem());
    item->setWidth(640);
    return render(&window);
}

// The parts of the lookup overlay and the editors, with synthetic values.
void TestUi::panelsLoad()
{
    QVERIFY(loadItem("ProgressPanel", {{"steps", QStringList{"Starting the browser", "Reading the listing", "Saving"}},
                                       {"step", 1},
                                       {"pages", 3},
                                       {"pagesToday", "eBay pages used today: 3 of 300"},
                                       {"lastLine", "reading the listing 100000000090"},
                                       {"browserName", "Example Browser"}}));
    QVERIFY(loadItem("BrowserPromptPanel", {{"message", "eBay asks you to sign in. Sign in in the browser window."},
                                            {"browserName", "Example Browser"}}));
    QVERIFY(loadItem("AskModelPanel", {{"title", "Acme meter 53 Z"},
                                       {"itemId", "100000000091"},
                                       {"reason", "several model-like words"},
                                       {"specifics", QStringList{"Brand: Acme", "Type: Meter"}},
                                       {"suggestions", QStringList{"Z53", "M7"}},
                                       {"brand", "Acme"},
                                       {"error", "Enter the brand and the model."}}));
    QVERIFY(loadItem("RangeBar", {{"minimum", 8000.0},
                                  {"maximum", 17000.0},
                                  {"bandLow", 9750.0},
                                  {"bandHigh", 11400.0},
                                  {"median", 10600.0},
                                  {"thisValue", 16500.0},
                                  {"thisLabel", "THIS $165.00"},
                                  {"cheapest", 10300.0},
                                  {"cheapestLabel", "cheapest for sale $103.00"}}));
    QVERIFY(loadItem("FlagCard", {{"kind", "warn"}, {"label", "SHIPPING"}, {"lines", QStringList{"$45.00 is 4.5x", "36%"}}}));
    QVERIFY(loadItem("ReviewSearchesPanel", {{"queries", QStringList{"Acme Q9", "Acme Corp Q9"}},
                                             {"soldDepth", 240},
                                             {"activeDepth", 240},
                                             {"researchDepth", 0},
                                             {"error", "Enter at least one search, one per line."}}));
    QVERIFY(loadItem("PriceHistoryScreen", {}));
    // The editor and the startup window are windows / popups of their own.
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.loadFromModule(kModule, "StartupError");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> window(component.createWithInitialProperties({{"message", "a synthetic problem"}}));
        QVERIFY(qobject_cast<QQuickWindow *>(window.get()));
        QVERIFY(render(qobject_cast<QQuickWindow *>(window.get())));
    }
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.loadFromModule(kModule, "IdentityEditor");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> popup(component.create());
        QVERIFY(popup);
    }
    QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QLatin1Char('\n'))));
}

// Owner case 10: every search came back empty -> a plain sentence instead of
// empty tables (and none while rows exist, see savedLookupReopens).
void TestUi::nothingFoundIsSaid()
{
    LookupResult result;
    result.status = QStringLiteral("complete");
    result.item.itemId = QStringLiteral("100000000095");
    result.item.title = QStringLiteral("Acme Z9 supply");
    result.item.condition = QStringLiteral("For parts or not working");
    result.identity.brandName = QStringLiteral("Acme");
    result.identity.model = QStringLiteral("Z9");
    result.identity.confident = true;
    result.queries = {QStringLiteral("Acme Z9"), QStringLiteral("Acme Corp Z9")};
    QString error;
    const std::optional<qint64> id = LookupStore(QStringLiteral("app")).save(
        QStringLiteral("100000000095"), result, {}, Valuation(), app::lookup::Depths{240, 240, 200}, 1095, &error);
    QVERIFY2(id, qPrintable(error));
    app::ui::Backend backend(*m_folder, QStringLiteral("app"));
    std::unique_ptr<app::ui::PhotoProvider> photos(backend.takePhotoProvider());
    QVERIFY(backend.openSaved(*id));
    QCOMPARE(backend.results()->emptyMessage(),
             QStringLiteral("eBay found nothing for \"Acme Z9\", \"Acme Corp Z9\" - check the brand and model "
                            "(Wrong item? Correct it)"));
    // The listing itself is flagged, on Results and on its Home card.
    QVERIFY(backend.results()->brokenFlag());
    QCOMPARE(backend.results()->brokenLine(),
             QStringLiteral("Likely broken: eBay says 'For parts or not working'. Sellers often don't test or don't say. "
                            "Assume it does not work."));
    app::ui::RecentModel *recent = backend.recent();
    bool cardFlagged = false;
    for (int i = 0; i < recent->count(); ++i) {
        const QModelIndex index = recent->index(i);
        if (index.data(app::ui::RecentModel::LookupIdRole).toLongLong() == *id)
            cardFlagged = index.data(app::ui::RecentModel::BrokenRole).toString() == QLatin1String("Likely broken");
    }
    QVERIFY(cardFlagged);
    QVERIFY(backend.openSaved(m_lookupId));
    QCOMPARE(backend.results()->emptyMessage(), QString());
}

// Owner decision 2026-10-19 "Buy It Now vs auction": the listing's format
// and auction card, Buy It Now-only cheapest, format badges, View only for
// what is for sale now, and the auction fields saved and read back.
void TestUi::auctionsAndFormats()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto save = [&](int bids, qint64 price, std::optional<QDateTime> end) -> qint64 {
        LookupResult result;
        result.status = QStringLiteral("complete");
        result.item.itemId = QStringLiteral("100000000096");
        result.item.title = QStringLiteral("Acme Q9 Signal Tracer");
        result.item.priceCents = price;
        result.item.shippingCents = 1000;
        result.item.condition = QStringLiteral("Used");
        result.item.auction = true;
        result.item.bids = bids;
        result.item.endsIn = QStringLiteral("3d 22h");
        result.item.endsAt = QStringLiteral("Friday, 06:28 PM");
        result.item.auctionEnd = end;
        result.identity.brandName = QStringLiteral("Acme");
        result.identity.model = QStringLiteral("Q9");
        result.identity.source = QStringLiteral("item specifics");
        result.identity.confident = true;
        result.queries = {QStringLiteral("Acme Q9")};
        result.sold = {sale("200000000001", "Acme Q9", 8000, 1000, "Used"), sale("200000000002", "Acme Q9", 9000, 1000, "Used"),
                       sale("200000000003", "Acme Q9", 10000, 1200, "Used"), sale("200000000004", "Acme Q9", 11000, 1000, "Used")};
        SearchRecord binOffer = sale("200000000021", "Acme Q9 tracer", 9500, 800, "Used");
        binOffer.bestOffer = true;
        SearchRecord running = sale("200000000022", "Acme Q9", 7000, 900, "Used");
        running.format = QStringLiteral("auction");
        running.bids = 3;
        running.timeLeft = QStringLiteral("2h 10m left");
        running.auctionEnd = now.addSecs(2 * 3600);
        SearchRecord ended = sale("200000000023", "Acme Q9 tracer", 6000, 900, "Used");
        ended.format = QStringLiteral("auction");
        ended.bids = 5;
        ended.auctionEnd = now.addSecs(-3600);
        result.active = {binOffer, running, ended};
        QList<Row> rows;
        for (qsizetype i = 0; i < 4; ++i)
            rows.append(row(Row::Source::Sold, i, Bucket::SameItem));
        for (qsizetype i = 0; i < 3; ++i)
            rows.append(row(Row::Source::Active, i, Bucket::SameItem));
        QString error;
        const std::optional<qint64> id = LookupStore(QStringLiteral("app")).save(
            QStringLiteral("100000000096"), result, rows, valuate(result, rows, app::ValueSettings()),
            app::lookup::Depths{240, 240, 200}, 1095, &error);
        return id ? *id : 0;
    };
    app::ui::Backend backend(*m_folder, QStringLiteral("app"));
    std::unique_ptr<app::ui::PhotoProvider> photos(backend.takePhotoProvider());
    app::ui::ResultsController *results = backend.results();

    // With bids: the current bid is not the final price; the bar says so.
    const qint64 withBids = save(3, 9000, std::nullopt);
    QVERIFY(withBids && backend.openSaved(withBids));
    QVERIFY(results->isAuction());
    QCOMPARE(results->formatText(), QStringLiteral("Auction - 3 bids, ends -"));
    QCOMPARE(results->auctionLabel(), QStringLiteral("AUCTION"));
    QCOMPARE(results->auctionLines(),
             QStringList{QStringLiteral("The current bid $90.00 is not the final price - it will likely go higher. Compare the "
                                        "sold numbers, not the current bid.")});
    QCOMPARE(results->thisLabel(), QStringLiteral("current bid $90.00"));
    QCOMPARE(results->auctionEndMs(), -1.0);
    // Cheapest = Buy It Now only: $103.00; the auctions are apart, the ended
    // one no longer running.
    QCOMPARE(results->forSaleText(), QStringLiteral("1, from $103.00"));
    QCOMPARE(results->auctionsLine(), QStringLiteral("Auctions running: 1, lowest current bid $70.00 (not a final price)"));
    QCOMPARE(results->cheapestItemId(), QStringLiteral("200000000021"));
    QMap<QString, QModelIndex> byId;
    for (int i = 0; i < results->sameItem()->count(); ++i) {
        const QModelIndex index = results->sameItem()->index(i);
        byId.insert(index.data(app::ui::RowsModel::ItemIdRole).toString(), index);
    }
    using R = app::ui::RowsModel;
    QCOMPARE(byId.value("200000000021").data(R::FormatRole).toString(), QStringLiteral("BEST OFFER"));
    QVERIFY(byId.value("200000000021").data(R::ViewRole).toBool());
    QVERIFY(byId.value("200000000021").data(R::ViewNoteRole).toString().startsWith(QLatin1String("listed when checked ")));
    QCOMPARE(byId.value("200000000022").data(R::FormatRole).toString(), QStringLiteral("AUCTION - 3 bids"));
    QVERIFY(byId.value("200000000022").data(R::ViewRole).toBool());
    QVERIFY(byId.value("200000000022").data(R::TimeLeftRole).toString().endsWith(QLatin1String(" left")));
    QCOMPARE(byId.value("200000000023").data(R::ViewRole).toBool(), false);
    QCOMPARE(byId.value("200000000023").data(R::TimeLeftRole).toString(), QStringLiteral("ENDED"));
    QCOMPARE(byId.value("200000000001").data(R::ViewRole).toBool(), false);
    QCOMPARE(byId.value("200000000001").data(R::FormatRole).toString(), QString());
    // The CHEAPEST mark is on the Buy It Now listing, not on an auction.
    QCOMPARE(byId.value("200000000021").data(R::MarkRole).toString(), QStringLiteral("cheapest"));
    QCOMPARE(byId.value("200000000022").data(R::MarkRole).toString(), QString());

    // No bids, start above the sold P75 ($114.00): starts high.
    const qint64 high = save(0, 12000, std::nullopt);
    QVERIFY(high && backend.openSaved(high));
    QCOMPARE(results->auctionLabel(), QStringLiteral("AUCTION STARTS HIGH"));
    QCOMPARE(results->auctionLines(),
             QStringList{QStringLiteral("The starting price $120.00 is already above what the same item sold for (median $106.00).")});
    QCOMPARE(results->thisLabel(), QStringLiteral("THIS $130.00"));

    // No bids, a low start, a known end: the countdown runs from the stored end.
    const QDateTime end = now.addSecs(3 * 86400);
    const qint64 low = save(0, 5000, end);
    QVERIFY(low && backend.openSaved(low));
    QCOMPARE(results->auctionLabel(), QStringLiteral("AUCTION"));
    QCOMPARE(results->auctionLines(), QStringList{QStringLiteral("Starts at $50.00 - the final price is decided by bidders.")});
    QVERIFY(results->formatText().startsWith(QLatin1String("Auction - 0 bids, ends ")));
    QVERIFY(results->auctionEndMs() > 0);
    QVERIFY(results->timeLeft(results->auctionEndMs(), false).startsWith(QLatin1String("2d 23h")));
    // Saved and read back.
    QString error;
    const std::optional<SavedLookup> saved = LookupStore(QStringLiteral("app")).load(low, &error);
    QVERIFY2(saved, qPrintable(error));
    QVERIFY(saved->result.item.auction);
    QCOMPARE(saved->result.item.bids, std::optional<int>(0));
    QCOMPARE(saved->result.item.endsAt, QStringLiteral("Friday, 06:28 PM"));
    QCOMPARE(saved->result.item.auctionEnd->toSecsSinceEpoch(), end.toSecsSinceEpoch());
    QVERIFY(saved->result.active.at(0).bestOffer);
    QCOMPARE(saved->result.active.at(1).timeLeft, QStringLiteral("2h 10m left"));
    QVERIFY(saved->result.active.at(1).auctionEnd.has_value());

    // A Buy It Now listing: its format, no auction card.
    QVERIFY(backend.openSaved(m_lookupId));
    QVERIFY(!results->isAuction());
    QCOMPARE(results->formatText(), QStringLiteral("-"));
    QVERIFY(results->auctionLines().isEmpty());
}

int main(int argc, char *argv[])
{
    // No display needed, and nothing written outside the test's folders
    // (the app's own settings: src/app/main.cpp).
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QML_DISK_CACHE", "aot");
    qunsetenv("QML_FORCE_DISK_CACHE");
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // As the app does at start (src/app/main.cpp); fontsAreBundled checks it.
    QString error;
    if (!app::ui::loadFonts(&error))
        fprintf(stderr, "WARNING: %s\n", qPrintable(error));
    TestUi test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ui.moc"
