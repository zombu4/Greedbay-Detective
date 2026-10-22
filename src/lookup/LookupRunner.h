#pragma once

#include "ebay/PageKind.h"
#include "lookup/Identity.h"
#include "lookup/KnownModels.h"
#include "lookup/MatchWords.h"
#include "lookup/Records.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace app::lookup {

// How many records a lookup collects at most per source (Settings
// lookup/soldDepth, activeDepth, researchDepth; chosen per lookup). 0 skips
// the source.
struct Depths {
    int sold = 0;
    int active = 0;
    int research = 0;
};

struct LookupResult {
    // "running" until finished(); then "complete", "cancelled" or "stopped".
    QString status = QStringLiteral("running");
    // For "stopped" and "cancelled": what happened, in plain words.
    QString message;
    ItemRecord item;
    Identity identity;
    QStringList queries;
    QList<SearchRecord> sold;
    QList<SearchRecord> active;
    QList<ResearchRecord> research;
    // The summary block of the first Product Research page read.
    QJsonObject researchSummary;
    // Product Research sent its Seller Hub start page: no access.
    bool researchUnavailable = false;
    // Pages the browser loaded and answered for (expected or not).
    int pagesLoaded = 0;
    // Item pages the Deep check loaded after the searches (0: it did not run).
    int deepCheckPages = 0;
    QStringList notes;
    qint64 elapsedMs = 0;
};

// One lookup (PLAN 5): the item page -> listing record -> identity (owner
// rules 1, 7, 21-28) -> one query per brand search name for the user to edit
// -> sold search, active search and Product Research, each paginated until
// its depth is reached or the queries have no more results -> records. When
// the identity is not confident nothing is searched: the user enters it
// (setIdentity()) first (rule 26). Matching and value are computed from the
// records (LookupReport); saving is LookupStore's (src/lookup/LookupStore.h).
//
// The runner does not drive the browser itself: it calls `fetcher` with the
// page to load and its kind, and gets the browser's reply through onReply()
// (the BrowserController done() reply objects). One page at a time; pacing
// and the daily cap are the browser controller's.
//
// Per source the queries are used in order: query 1 page by page while its
// pages say more results exist and the depth is not reached, then query 2,
// and so on. A record whose item number an earlier page (of this or an
// earlier query of the same source) already gave is skipped: the same listing
// found again, for example when new listings shift the pages. Repeats within
// one page are kept (they may be separate sales of one listing; not observed
// either way). The last page is cut at the depth. When a page cannot tell
// whether more pages exist, paging stops there with a note.
//
// Owner decision Q22: without Product Research access every price is
// unconfirmed, so when eBay sends the Seller Hub start page every sold
// record is marked unconfirmed.
//
// Replies: "ok" -> records; "unexpected_page" with page "no_access" for
// Product Research -> research is skipped (every price then comes from the
// sold search) and the lookup goes on; any other unexpected page, a
// "failed" or a "limit_reached" reply stops the lookup with what was
// collected (status "stopped"; "failed: stopped" from a browser cancel ->
// "cancelled"). cancel() stops after the page being loaded.
class LookupRunner : public QObject {
    Q_OBJECT
public:
    using Fetcher = std::function<void(const QUrl &url, ebay::PageKind kind)>;

    // `known` and `words` must outlive the runner.
    LookupRunner(Fetcher fetcher, const KnownModels *known, const MatchWords *words, QObject *parent = nullptr);

    // Reads the item page of `input` (an item address or number, see
    // ebay::itemIdFromInput). False (and *error) for input that is not one;
    // nothing is fetched then.
    bool startItem(const QString &input, QString *error);

    // Instead of startItem(): takes the listing record of a saved lookup
    // (the item page is not loaded again), identifies it as startItem() does
    // (an identity the user saved for it comes first) and emits itemReady()
    // from the event loop. False (and *error) when a lookup is already
    // running or the record has no item number.
    bool startFromItem(const ItemRecord &item, QString *error);

    // After itemReady(): the identity the user entered or corrected; the
    // queries are built anew from it. False (and *error) when not waiting for
    // queries.
    bool setIdentity(const ConfirmedIdentity &entered, QString *error);

    // After itemReady(): fetches the comparison pages for `queries` (empty
    // entries are dropped). `dayRange` is Product Research's period in days.
    // False (and *error) when not waiting for queries, the identity has no
    // brand and model to search for, no query is left, or a depth is
    // negative.
    bool startComps(const QStringList &queries, const Depths &depths, int dayRange, QString *error);

    // The browser's reply to the last fetch.
    void onReply(const QJsonObject &reply);

    // Stops after the page being loaded; at once when no page is being loaded.
    void cancel();

    // True while a page is being loaded for the runner.
    bool isFetching() const { return m_fetching; }
    bool isWaitingForQueries() const { return m_state == State::WaitingForQueries; }
    const LookupResult &result() const { return m_result; }

signals:
    void progress(const QString &line);
    // The item page was read: result().item, identity and queries are set
    // (no queries when the identity is not confident).
    void itemReady();
    void finished();

private:
    enum class State { Idle, Item, WaitingForQueries, Comps, Done };
    enum class Source { Sold = 0, Active = 1, Research = 2, End = 3 };

    void fetch(const QUrl &url, ebay::PageKind kind);
    void onItemReply(const QJsonObject &reply);
    void onCompsReply(const QJsonObject &reply);
    // Takes the records of one page; true when this query has more pages to read.
    bool takePage(const QJsonObject &data);
    void nextFetch();
    void nextQuery();
    void nextSource();
    int depthOf(Source source) const;
    int collectedOf(Source source) const;
    QString sourceName(Source source) const;
    void finish(const QString &status, const QString &message);

    Fetcher m_fetcher;
    const KnownModels *m_known;
    const MatchWords *m_words;
    State m_state = State::Idle;
    bool m_fetching = false;
    bool m_cancelRequested = false;
    LookupResult m_result;
    QString m_itemId;
    Depths m_depths;
    int m_dayRange = 0;
    QElapsedTimer m_clock;

    Source m_source = Source::Sold;
    int m_queryIndex = 0;
    int m_page = 1;    // search pages, from 1
    int m_offset = 0;  // Product Research rows
    // Item numbers given by earlier queries / by earlier pages of the current
    // query, in the current source.
    QSet<QString> m_seenBefore;
    QSet<QString> m_seenNow;
};

} // namespace app::lookup
