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

// The numbers (owner rules 11, 12, 15, 16, 18, 20; DESIGN 3.4), from the
// included rows only:
//   sold      per condition level, from the sold search (confirmed prices;
//             unconfirmed Best Offer sales counted apart)
//   research  per condition level, from Product Research rows: one value per
//             listing = average sold price + average shipping
//   shipping  the listing's shipping against the included sold comps and
//             eBay's Product Research average shipping
//   ebay      eBay's own Product Research summary, only as a labeled side
//             figure (it may mix models, rule 20)
struct Valuation {
    Match listing; // the listing's own condition
    QList<ConditionNumbers> sold;
    QList<ConditionNumbers> research;
    ShippingCheck shipping;
    std::optional<qint64> ebayAverageSoldCents;
};

Valuation valuate(const LookupResult &result, const QList<Row> &rows, const MatchWords &words,
                  const ValueSettings &settings);

// "$1,234.56"; "-$12.00" for a negative amount.
QString money(qint64 cents);

// The identity, the queries, the numbers and every row by bucket, as plain
// text. `expectation`: what the user thinks it is worth (shown next to the
// market numbers, rule 12; empty for none). Titles are cut to 60 characters.
QString reportText(const LookupResult &result, const QList<Row> &rows, const Valuation &valuation,
                   const QString &expectation);

} // namespace app::lookup
