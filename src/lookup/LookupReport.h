#pragma once

#include "lookup/LookupRunner.h"

#include <QString>

#include <optional>

namespace app::lookup {

// The numbers of a lookup, phase-3b form (no matching, no verdict words):
// every confirmed sold-search record found counts (matching comes in phase
// 4); Product Research rows, averages per listing, are not in them.
struct LookupNumbers {
    // Confirmed sold records (sold search, no "Best offer accepted", and
    // Product Research not refused) with a known total (price + shipping).
    int confirmedWithTotal = 0;
    std::optional<qint64> medianCents;
    std::optional<qint64> p25Cents;
    std::optional<qint64> p75Cents;
    // Median shipping of the confirmed sold records whose shipping is known.
    int confirmedWithShipping = 0;
    std::optional<qint64> medianShippingCents;
};

LookupNumbers computeNumbers(const LookupResult &result);

// "$1,234.56"; "-$12.00" for a negative amount.
QString money(qint64 cents);

// The whole result as plain-text tables: the listing, sold (confirmed, then
// unconfirmed Best Offer), for sale now, Product Research rows, then the
// numbers: median and P25-P75 of confirmed sold totals, where the listing's
// total stands against that range, and its shipping against the median
// shipping. Titles are cut to 60 characters.
QString reportText(const LookupResult &result);

} // namespace app::lookup
