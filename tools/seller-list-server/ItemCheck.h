#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace sellerserver {

// The server's one check of a reported item on eBay's public item page
// (design: "the item exists, was sold by that seller"). Read from what the
// page recon observed on the rendered item page:
//   - the JSON-LD block <script type="application/ld+json"> holding an
//     "ItemPage" (the page is an item page);
//   - the seller's feedback links /fdbk/mweb_profile?fdbkType=
//     FeedbackReceivedAsSeller&item_id=<item>&username=<seller id>.
// Whether the item SOLD is not read: an ended, sold item page was never
// observed, so nothing about it is assumed. Anything the reader cannot
// confirm is NotReadable, and the report is held for the owner, never
// published on a guess.
enum class ItemVerdict {
    Confirmed,   // an item page of this item, by the reported seller
    OtherSeller, // an item page of this item, by another seller
    NotReadable, // no item page, no seller link, links disagree, or no page at all
};

struct ItemReading {
    ItemVerdict verdict = ItemVerdict::NotReadable;
    QString why; // for the owner's queue
};

ItemReading readItemPage(const QByteArray &html, const QString &itemId, const QString &sellerId);

// Loads item pages one at a time, at least `gapMs` apart (a project choice
// for a small server: one page per report, paced), each with a time and size
// limit. Same-origin redirects only. On HTTP 403 or 429 it stops loading for
// `pauseMs` (no retry around a block) and every waiting check ends as
// NotReadable.
class ItemPageFetcher : public QObject {
    Q_OBJECT
public:
    struct Settings {
        QUrl base = QUrl(QStringLiteral("https://www.ebay.com")); // tests use a local server
        int gapMs = 10 * 1000;
        int timeoutMs = 30 * 1000;
        qint64 maxBytes = 4 * 1024 * 1024;
        int pauseMs = 60 * 60 * 1000;
        int maxQueue = 200;
        QByteArray userAgent = "seller-list-server/1";
    };

    explicit ItemPageFetcher(Settings settings, QObject *parent = nullptr);
    ~ItemPageFetcher() override;

    // Queues a check; finished(reportId, reading) follows. A full queue ends
    // at once as NotReadable.
    void check(const QString &reportId, const QString &itemId, const QString &sellerId);
    int waiting() const { return static_cast<int>(m_queue.size()); }

signals:
    void finished(const QString &reportId, const sellerserver::ItemReading &reading);

private:
    struct Job {
        QString reportId;
        QString itemId;
        QString sellerId;
    };

    void startNext();
    void onReplyFinished();
    void finishJob(const ItemReading &reading);

    Settings m_settings;
    QNetworkAccessManager *m_network;
    QTimer *m_gap;
    QList<Job> m_queue;
    QNetworkReply *m_reply = nullptr;
    QDateTime m_pausedUntil;
};

} // namespace sellerserver
