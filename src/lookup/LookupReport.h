#pragma once

#include "core/Settings.h"
#include "lookup/LookupRunner.h"
#include "lookup/Matcher.h"
#include "lookup/Value.h"

#include <QList>
#include <QString>

#include <optional>

namespace app::lookup {

// One result row with its match. `included` says whether it counts in the
// numbers: same-item rows are included unless the user unticks them; any
// other row only when the user ticks it.
struct Row {
    enum class Source { Sold, Active, Research };
    Source source = Source::Sold;
    qsizetype index = 0; // into result.sold / active / research
    Match match;
    bool included = false;
};

// Every row of `result`, matched against its identity.
QList<Row> classifyRows(const LookupResult &result, const MatchWords &words);

// Owner case 11: the pasted listing is never "not this item".
inline constexpr char kIdentityMismatch[] =
    "The model doesn't match this listing's own title - check it (Wrong item? Correct it)";
// After classifyRows (and the matcher plugins), before the numbers:
//  - an identity the user set (source "user" or "saved"): the listing's own
//    rows (its item number) count as the same item;
//  - any other identity that the listing's own title does not match
//    (matchRow gives "not this item") is wrong: no row is counted, so no
//    numbers come from it, and kIdentityMismatch is added to result->notes.
// False when the identity is wrong.
bool checkOwnListing(LookupResult *result, QList<Row> *rows, const MatchWords &words);

// The numbers, from the included rows only (owner decision 2026-10-19: no
// condition grading - one set of numbers over all same-item rows; rules 11,
// 12, 16, 20; DESIGN 3.4):
//   sold      from the sold search (confirmed prices; unconfirmed Best Offer
//             sales counted apart)
//   research  from Product Research rows: one value per listing = average
//             sold price + average shipping
//   shipping  the listing's shipping against the included sold comps and
//             eBay's Product Research average shipping
//   ebay      eBay's own Product Research summary, only as a labeled side
//             figure (it may mix models, rule 20)
//   forSale   totals of the included Buy It Now rows (with or without Best
//             Offer; format "fixed_price") other than the pasted listing,
//             lowest first; cheaperForSale counts those below the pasted
//             listing's total (owner decision 2026-10-19: an auction's low
//             start is not a low price)
//   auctions  current prices of the included running auctions other than
//             the pasted listing, lowest first (not final prices); for-sale
//             rows whose format eBay did not show count in neither
struct Valuation {
    ValueNumbers sold;
    ValueNumbers research;
    ShippingCheck shipping;
    std::optional<qint64> ebayAverageSoldCents;
    QList<qint64> forSale;
    int cheaperForSale = 0;
    QList<qint64> auctions;
};

Valuation valuate(const LookupResult &result, const QList<Row> &rows, const ValueSettings &settings);

// A row's total (price + shipping; for Product Research average sold +
// average shipping), its eBay condition text as eBay gives it (empty for
// Product Research rows) and its title.
std::optional<qint64> rowTotal(const LookupResult &result, const Row &row);
QString rowCondition(const LookupResult &result, const Row &row);
QString rowTitle(const LookupResult &result, const Row &row);
// A row's item number, its date (sold rows: the sold date; Product Research
// rows: the last sold date; for-sale rows: none), its seller (none for
// Product Research rows), and its source as "sold", "active" or "research".
QString rowItemId(const LookupResult &result, const Row &row);
QString rowDate(const LookupResult &result, const Row &row);
QString rowSeller(const LookupResult &result, const Row &row);
QString rowSourceName(Row::Source source);

// The rows in report order: by bucket (same item first), then by total,
// lowest first; rows without a total last.
QList<qsizetype> rowOrder(const LookupResult &result, const QList<Row> &rows);

// How many of the cheapest included for-sale rows are marked.
constexpr int kCheapestMarked = 3;

// "$1,234.56"; "-$12.00" for a negative amount.
QString money(qint64 cents);

// The identity, the queries, the numbers, the pasted listing against the
// sold range and the cheapest same-item listings for sale now, and every row
// by bucket sorted by total, as plain text. `expectation`: what the user
// thinks it is worth (shown next to the market numbers, rule 12; empty for
// none). Titles are cut to 60 characters.
QString reportText(const LookupResult &result, const QList<Row> &rows, const Valuation &valuation,
                   const QString &expectation);

} // namespace app::lookup
