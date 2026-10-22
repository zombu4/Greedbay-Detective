// Paging decisions without a browser: the purchase check (which page next,
// which order counts, when it stops) and the review read (pages added up to
// eBay's 12-month count, the page cap, a failure after some pages keeps the
// part). Replies are recorded-form JSON with made-up sellers and items.

#include "core/WorkFolder.h"
#include "db/Database.h"
#include "db/Migrations.h"
#include "lookup/SellerStore.h"
#include "ui/PurchaseCheck.h"
#include "ui/SellerSession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

using app::ui::PurchaseCheck;
using app::ui::SellerSession;

namespace {

QJsonObject ok(const QJsonObject &result) { return {{"status", "ok"}, {"result", result}}; }

QJsonObject order(const QString &seller, const QString &item, const QJsonValue &month)
{
    return {{"itemId", item}, {"sellerId", seller}, {"month", month}};
}

QJsonObject purchasesPage(int page, bool hasNext, const QJsonArray &orders)
{
    return ok({{"orders", orders}, {"page", page}, {"hasNext", hasNext}});
}

QJsonArray otherOrders(int count, int start)
{
    QJsonArray orders;
    for (int n = 0; n < count; ++n)
        orders.append(order(QStringLiteral("shop_%1").arg(start + n), QString::number(100000000000LL + start + n), "2026-05"));
    return orders;
}

QJsonObject reviewsPage(int page, bool hasNext, int first, int count, int negatives12)
{
    QJsonArray reviews;
    for (int n = first; n < first + count; ++n)
        reviews.append(QJsonObject{{"when", "Past month"}, {"comment", QStringLiteral("Bad order %1").arg(n)}, {"item", "Made-up part"},
                                   {"price", "US $1.00"}, {"verified", true}});
    return ok({{"sellerId", "made_up_seller"}, {"kind", "negative"}, {"positive", QJsonArray{1, 2, 3}},
               {"neutral", QJsonArray{0, 0, 0}}, {"negative", QJsonArray{0, 1, negatives12}}, {"score", 100},
               {"reviews", reviews}, {"page", page}, {"hasNext", hasNext}});
}

QList<app::Migration> shippedMigrations()
{
    QString error;
    const auto migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
    return migrations ? *migrations : QList<app::Migration>{};
}

} // namespace

class TestSellerPaging : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void purchaseFoundOnSecondPage();
    void purchaseNeedsItemAndMonth();
    void purchaseNotFound();
    void purchaseItemCheck();
    void purchasePageCap();
    void purchaseLimitReached();
    void reviewsAddedUpAcrossPages();
    void reviewsStopAtTheCount();
    void reviewsPageCap();
    void reviewsFailureKeepsThePart();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<app::WorkFolder> m_folder;
};

void TestSellerPaging::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_folder = app::WorkFolder::open(m_tmp->filePath(QStringLiteral("work")), QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(m_folder, qPrintable(error));
    QVERIFY(app::Database::open(QStringLiteral("main"), m_folder->databasePath(), m_folder->backupsDir(),
                                QStringLiteral(TEST_APP_NAME), shippedMigrations())
                .ok());
}

void TestSellerPaging::cleanup()
{
    QSqlDatabase::database(QStringLiteral("main"), false).close();
    QSqlDatabase::removeDatabase(QStringLiteral("main"));
    m_folder.reset();
    m_tmp.reset();
}

void TestSellerPaging::purchaseFoundOnSecondPage()
{
    PurchaseCheck check(*m_folder);
    check.m_sellerId = QStringLiteral("Wanted_Seller");
    QCOMPARE(check.onReply(purchasesPage(1, true, otherOrders(25, 1))), std::optional<QString>(QStringLiteral("@2")));
    QJsonArray second = otherOrders(2, 26);
    second.append(order(QStringLiteral("wanted_seller"), QStringLiteral("200000000027"), "2026-04"));
    QCOMPARE(check.onReply(purchasesPage(2, false, second)), std::nullopt);
    QVERIFY(check.found());
    QCOMPARE(check.found()->itemId, QStringLiteral("200000000027"));
    QCOMPARE(check.found()->month, QStringLiteral("2026-04"));
    QCOMPARE(check.error(), QString());
}

void TestSellerPaging::purchaseItemCheck()
{
    // After the order is found, its item page (owner decision a, 2026-10-21):
    // the page of that item, naming the reported seller, is "item checked";
    // its sold state is read; anything else leaves the item unchecked.
    auto itemReply = [](const QString &itemId, const QString &sellerId, const QString &availability, bool soldNotice) {
        return QJsonObject{{"status", "ok"},
                           {"result", QJsonObject{{"itemId", itemId}, {"seller", QJsonObject{{"id", sellerId}}},
                                                  {"availability", availability}, {"soldNotice", soldNotice}}}};
    };
    struct Case {
        QJsonObject reply;
        bool checked;
        QString state;
    };
    const QString item = QStringLiteral("200000000027");
    const QList<Case> cases{
        {itemReply(item, QStringLiteral("WANTED_seller"), QStringLiteral("OutOfStock"), true), true, QStringLiteral("sold")},
        {itemReply(item, QStringLiteral("wanted_seller"), QStringLiteral("InStock"), false), true, QStringLiteral("for sale")},
        {itemReply(item, QStringLiteral("someone_else"), QStringLiteral("OutOfStock"), true), false, QStringLiteral("sold")},
        {itemReply(QStringLiteral("200000000099"), QStringLiteral("wanted_seller"), QStringLiteral("InStock"), false), false,
         QStringLiteral("unknown")},
        {QJsonObject{{"status", "unexpected_page"}, {"reason", "x"}}, false, QStringLiteral("unknown")},
    };
    for (const Case &c : cases) {
        PurchaseCheck check(*m_folder);
        check.m_sellerId = QStringLiteral("Wanted_Seller");
        QJsonArray orders;
        orders.append(order(QStringLiteral("wanted_seller"), item, "2026-04"));
        QCOMPARE(check.onReply(purchasesPage(1, false, orders)), std::nullopt);
        QVERIFY(check.m_checkingItem);
        QVERIFY(!check.found()->itemChecked);
        QCOMPARE(check.found()->itemNote, QStringLiteral("the item was not checked"));
        QCOMPARE(check.onReply(c.reply), std::nullopt);
        QVERIFY(!check.m_checkingItem);
        QCOMPARE(check.found()->itemChecked, c.checked);
        QCOMPARE(check.found()->itemState, c.state);
        QVERIFY(!check.found()->itemNote.isEmpty());
    }
}

void TestSellerPaging::purchaseNeedsItemAndMonth()
{
    // An order from the seller without a readable month or item number proves
    // nothing; a later readable one does.
    PurchaseCheck check(*m_folder);
    check.m_sellerId = QStringLiteral("wanted_seller");
    const QJsonArray orders{order(QStringLiteral("wanted_seller"), QStringLiteral("300000000001"), QJsonValue()),
                            order(QStringLiteral("wanted_seller"), QStringLiteral("12ab"), "2026-03"),
                            order(QStringLiteral("wanted_seller"), QStringLiteral("300000000003"), "2026-13"),
                            order(QStringLiteral("wanted_seller"), QStringLiteral("300000000004"), "2026-02")};
    QCOMPARE(check.onReply(purchasesPage(1, false, orders)), std::nullopt);
    QVERIFY(check.found());
    QCOMPARE(check.found()->itemId, QStringLiteral("300000000004"));
}

void TestSellerPaging::purchaseNotFound()
{
    PurchaseCheck check(*m_folder);
    check.m_sellerId = QStringLiteral("wanted_seller");
    QCOMPARE(check.onReply(purchasesPage(1, true, otherOrders(25, 1))), std::optional<QString>(QStringLiteral("@2")));
    QCOMPARE(check.onReply(purchasesPage(2, false, otherOrders(3, 26))), std::nullopt);
    QVERIFY(!check.found());
    QVERIFY2(check.error().contains(QStringLiteral("last 28 orders")), qPrintable(check.error()));
}

void TestSellerPaging::purchasePageCap()
{
    PurchaseCheck check(*m_folder);
    check.m_sellerId = QStringLiteral("wanted_seller");
    for (int page = 1; page < PurchaseCheck::kMaxPages; ++page)
        QCOMPARE(check.onReply(purchasesPage(page, true, otherOrders(25, page * 25))),
                 std::optional<QString>(QStringLiteral("@%1").arg(page + 1)));
    QCOMPARE(check.onReply(purchasesPage(PurchaseCheck::kMaxPages, true, otherOrders(25, 9000))), std::nullopt);
    QVERIFY(!check.found());
    QVERIFY(check.error().contains(QStringLiteral("last %1 orders").arg(PurchaseCheck::kMaxPages * 25)));
}

void TestSellerPaging::purchaseLimitReached()
{
    PurchaseCheck check(*m_folder);
    check.m_sellerId = QStringLiteral("wanted_seller");
    QCOMPARE(check.onReply(QJsonObject{{"status", "limit_reached"}}), std::nullopt);
    QVERIFY(!check.found());
    QVERIFY(check.error().contains(QStringLiteral("page limit")));
}

void TestSellerPaging::reviewsAddedUpAcrossPages()
{
    SellerSession session(*m_folder, QStringLiteral("main"));
    session.m_sellerId = QStringLiteral("made_up_seller");
    session.m_kind = QStringLiteral("negative");
    QSignalSpy loaded(&session, &SellerSession::loaded);
    QCOMPARE(session.onReply(reviewsPage(1, true, 1, 25, 30)), std::optional<QString>(QStringLiteral("negative@2")));
    QCOMPARE(session.onReply(reviewsPage(2, false, 26, 5, 30)), std::nullopt);
    QCOMPARE(loaded.size(), 1);
    QString error;
    const std::optional<app::lookup::SellerFeedback> stored
        = app::lookup::SellerStore(QStringLiteral("main")).feedback(QStringLiteral("made_up_seller"), QStringLiteral("negative"), &error);
    QVERIFY2(stored, qPrintable(error));
    QCOMPARE(stored->reviews.size(), 30);
    QCOMPARE(stored->reviews.last().comment, QStringLiteral("Bad order 30"));
    QVERIFY(stored->complete);
    QCOMPARE(stored->note, QString());
}

void TestSellerPaging::reviewsStopAtTheCount()
{
    // eBay counts 25 in 12 months: a next page (older reviews) is not read.
    SellerSession session(*m_folder, QStringLiteral("main"));
    session.m_sellerId = QStringLiteral("made_up_seller");
    session.m_kind = QStringLiteral("negative");
    QCOMPARE(session.onReply(reviewsPage(1, true, 1, 25, 25)), std::nullopt);
    QString error;
    QCOMPARE(app::lookup::SellerStore(QStringLiteral("main"))
                 .feedback(QStringLiteral("made_up_seller"), QStringLiteral("negative"), &error)
                 ->reviews.size(),
             25);
}

void TestSellerPaging::reviewsPageCap()
{
    SellerSession session(*m_folder, QStringLiteral("main"));
    session.m_sellerId = QStringLiteral("made_up_seller");
    session.m_kind = QStringLiteral("negative");
    const int count = (SellerSession::kMaxPages + 5) * 25;
    for (int page = 1; page < SellerSession::kMaxPages; ++page)
        QCOMPARE(session.onReply(reviewsPage(page, true, (page - 1) * 25 + 1, 25, count)),
                 std::optional<QString>(QStringLiteral("negative@%1").arg(page + 1)));
    QCOMPARE(session.onReply(reviewsPage(SellerSession::kMaxPages, true, 9000, 25, count)), std::nullopt);
    QString error;
    const std::optional<app::lookup::SellerFeedback> stored
        = app::lookup::SellerStore(QStringLiteral("main")).feedback(QStringLiteral("made_up_seller"), QStringLiteral("negative"), &error);
    QVERIFY(stored);
    QVERIFY(!stored->complete);
    QVERIFY2(stored->note.contains(QStringLiteral("at most %1 pages").arg(SellerSession::kMaxPages)), qPrintable(stored->note));
}

void TestSellerPaging::reviewsFailureKeepsThePart()
{
    SellerSession session(*m_folder, QStringLiteral("main"));
    session.m_sellerId = QStringLiteral("made_up_seller");
    session.m_kind = QStringLiteral("negative");
    QCOMPARE(session.onReply(reviewsPage(1, true, 1, 25, 60)), std::optional<QString>(QStringLiteral("negative@2")));
    QCOMPARE(session.onReply(QJsonObject{{"status", "unexpected_page"}, {"reason", "the list did not move to page 2"}}), std::nullopt);
    QString error;
    const std::optional<app::lookup::SellerFeedback> stored
        = app::lookup::SellerStore(QStringLiteral("main")).feedback(QStringLiteral("made_up_seller"), QStringLiteral("negative"), &error);
    QVERIFY(stored);
    QCOMPARE(stored->reviews.size(), 25);
    QVERIFY(!stored->complete);
    QVERIFY2(stored->note.contains(QStringLiteral("did not move to page 2")), qPrintable(stored->note));
    QCOMPARE(session.error(), QString());
}

QTEST_GUILESS_MAIN(TestSellerPaging)
#include "tst_sellerpaging.moc"
