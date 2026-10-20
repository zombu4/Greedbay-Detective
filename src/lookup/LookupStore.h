#pragma once

#include "lookup/KnownModels.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace app::lookup {

// A lookup as saved (schema version 2, migration 0002_saved_lookups.sql):
// everything needed to show it again without loading anything from eBay.
// result.identity holds only what is shown (brand, model, product name,
// family, known issues, source, confident, reason); its match names are not
// saved, because the saved rows keep the bucket and reasons they were given.
struct SavedLookup {
    qint64 id = 0;
    QDateTime createdAt; // UTC
    QString input;
    LookupResult result;
    QList<Row> rows;
    Depths depths;
    int historyDays = 0;
};

// One line of the recent-lookups list: the numbers as last computed.
struct LookupSummary {
    qint64 id = 0;
    QDateTime createdAt; // UTC
    QString input; // the link or item number the lookup was started with
    QString itemId;
    QString title;
    QString conditionText; // eBay's condition text of the listing
    QString brand;
    QString model;
    QString status; // "complete", "partial" or "failed"
    std::optional<qint64> listingTotalCents;
    int soldCount = 0;
    std::optional<qint64> soldMedianCents;
    QString soldConfidence;
    bool shippingFlagged = false;
    std::optional<double> shippingFlagRatio;
    int forSaleCount = 0;
    int cheaperForSale = 0;
    std::optional<qint64> cheapestForSaleCents;
};

// A sale row of a saved lookup, for the price history: a sold-search row
// whose price is the price paid (not an unconfirmed Best Offer; its sold
// date), or a Product Research row of exactly one sale (its last sold date;
// its average is that one sale's price). Total = price + shipping; none when
// either is unknown.
struct HistorySaleRow {
    qint64 lookupId = 0;
    QString itemId;
    QString title;
    QDate soldOn;
    std::optional<qint64> totalCents;
    bool included = false; // counted in its lookup (the user's ticks)
};

// Saves and reads lookups in the app's database (the open QSQLITE connection
// `connectionName`, see db/Database). Every write is one transaction: either
// all of it is stored or nothing.
//
// Status: the runner's "complete" is stored as "complete", "cancelled" as
// "partial", "stopped" as "failed" (PLAN 5: cancel saves partial, an error
// saves failed with what was collected); reading maps them back.
//
// Never stored: page HTML, the user's ZIP code, cookies, the user's own eBay
// account name (the records hold none of them; seller names of listings are
// public listing data and are stored).
class LookupStore {
public:
    explicit LookupStore(QString connectionName);

    // Saves a finished lookup: the listing, identity, queries, every row with
    // its bucket, reasons and checkbox, and the numbers. The new lookup's id,
    // or std::nullopt (and *error).
    std::optional<qint64> save(const QString &input, const LookupResult &result, const QList<Row> &rows,
                               const Valuation &valuation, const Depths &depths, int historyDays,
                               QString *error) const;

    // Stores the user's checkboxes (rows' `included`, in the order load() or
    // save() had them) and the numbers computed from them.
    bool updateIncluded(qint64 lookupId, const LookupResult &result, const QList<Row> &rows,
                        const Valuation &valuation, QString *error) const;

    std::optional<SavedLookup> load(qint64 lookupId, QString *error) const;

    // The newest `limit` lookups, newest first; every lookup when `limit` is
    // 0 or less.
    std::optional<QList<LookupSummary>> recent(int limit, QString *error) const;

    // The sale rows (see HistorySaleRow) of the lookups `lookupIds`, newest
    // lookup first, in each lookup in their stored order.
    std::optional<QList<HistorySaleRow>> historySales(const QList<qint64> &lookupIds, QString *error) const;

    // Records an identity the user entered or corrected for a listing.
    bool saveUserIdentity(const QString &itemId, const ConfirmedIdentity &identity, std::optional<qint64> lookupId,
                          QString *error) const;
    // The identities recorded for `itemId`, oldest first.
    std::optional<QList<ConfirmedIdentity>> userIdentities(const QString &itemId, QString *error) const;

    // The listing's photo (JPEG bytes), one per item number; a new one
    // replaces the old.
    bool savePhoto(const QString &itemId, const QString &imageUrl, const QByteArray &jpeg, QString *error) const;
    // Empty when none is saved (or it cannot be read).
    QByteArray photo(const QString &itemId) const;

    // "same_item", "documentation", ... as stored; and back (std::nullopt for
    // anything else).
    static QString bucketId(Bucket bucket);
    static std::optional<Bucket> bucketFromId(const QString &id);

private:
    QString m_connectionName;
};

} // namespace app::lookup
