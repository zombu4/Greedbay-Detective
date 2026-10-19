#pragma once

#include "core/Settings.h"
#include "lookup/Matcher.h"

#include <QList>
#include <QString>

#include <optional>

namespace app::lookup {

// One comparable sale: its total (price + shipping), shipping, whether the
// price is the price paid, and its condition level.
struct Comp {
    std::optional<qint64> totalCents;
    std::optional<qint64> shippingCents;
    bool confirmed = false;
    Condition condition = Condition::NotStated;
};

// The numbers of one condition level (owner rules 15, 18; DESIGN 3.4):
// confirmed totals only; totals outside [P25 - f x IQR, P75 + f x IQR]
// (computed on all confirmed totals of the level) are outliers, listed and
// left out; median and P25-P75 of the rest (Stats::percentile); confidence
// by how many are left. Unconfirmed sales are counted apart, with their own
// median, never mixed in.
struct ConditionNumbers {
    Condition condition = Condition::NotStated;
    int included = 0;
    QList<qint64> outliers;
    std::optional<qint64> medianCents;
    std::optional<qint64> p25Cents;
    std::optional<qint64> p75Cents;
    QString confidence;
    int unconfirmed = 0;
    std::optional<qint64> unconfirmedMedianCents;
};

// The levels that have comps, in scale order (new, working, not stated,
// untested, non-working). Comps without a total are not counted.
QList<ConditionNumbers> valueByCondition(const QList<Comp> &comps, const ValueSettings &settings);

// "High", "Medium", "Low" or "Not enough data" for `count` sales.
QString confidenceName(int count, const ValueSettings &settings);

// The listing's shipping against the median shipping of the comps (the
// matched, confirmed same-item sales, any condition; rules 11, 16) and
// against eBay's Product Research average (a side figure, rule 20).
struct ShippingCheck {
    std::optional<qint64> listingCents;
    int compsWithShipping = 0;
    std::optional<qint64> compMedianCents;
    std::optional<qint64> ebayAverageCents;
    // Against each reference (rule 16): listing > ratio x reference AND more
    // than the minimum above it. flagged: against either.
    bool flaggedVsComps = false;
    bool flaggedVsEbay = false;
    bool flagged = false;
};

ShippingCheck checkShipping(std::optional<qint64> listingCents, const QList<Comp> &comps,
                            std::optional<qint64> ebayAverageCents, const ValueSettings &settings);

} // namespace app::lookup
