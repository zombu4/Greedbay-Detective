#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace app::lookup {

// Listing records built from the extractors' JSON (src/browser/extractors/),
// one struct per source page. Money is in integer cents (USD); a value the
// page did not show is std::nullopt, never a guess.

// One search card (active or sold search).
struct SearchRecord {
    QString itemId;
    QString title;
    QString condition;
    // Only when the card shows one price; a price range (listing with
    // variations) or an unreadable price leaves it empty.
    std::optional<qint64> priceCents;
    QString priceText;
    std::optional<qint64> shippingCents;
    QString format; // "auction", "fixed_price" or empty when not shown
    std::optional<int> bids;
    QString sellerName;
    // Sold search only. soldDate is YYYY-MM-DD or empty. confirmed: the shown
    // price is what the buyer paid (no "Best offer accepted" on the card,
    // recon section 2); unconfirmed: the accepted offer is not on the page.
    QString soldDate;
    bool confirmed = false;

    // price + shipping when both are known.
    std::optional<qint64> totalCents() const;

    // From one entry of the search extractor's items[]; `sold` for the sold
    // search.
    static SearchRecord fromJson(const QJsonObject &item, bool sold);
};

// One Product Research row: one listing, aggregated over its sales (recon
// section 4). avgSoldPriceCents is the average price actually paid.
struct ResearchRecord {
    QString itemId;
    QString title;
    std::optional<qint64> avgSoldPriceCents;
    std::optional<qint64> avgShippingCents;
    QString formatText;
    std::optional<int> totalSold;
    QString lastSoldDate;

    static ResearchRecord fromJson(const QJsonObject &row);
};

// The pasted listing, from the item extractor.
struct ItemRecord {
    QString itemId;
    QString title;
    std::optional<qint64> priceCents; // only when the currency is USD
    QString currency;
    std::optional<qint64> shippingCents;
    QString condition;
    QString sellerName;
    struct Specific {
        QString label;
        QString value;
    };
    QList<Specific> specifics;

    std::optional<qint64> totalCents() const;
    // The value of the first item specific whose label is `label` (compared
    // without case), or an empty string.
    QString specific(const QString &label) const;

    static ItemRecord fromJson(const QJsonObject &data);
};

} // namespace app::lookup
