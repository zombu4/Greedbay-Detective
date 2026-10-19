#pragma once

#include <QList>

#include <optional>

namespace app::lookup {

// The p-th percentile (0 <= p <= 100) of `values`, by linear interpolation
// between the closest ranks: with the values sorted as x[0..n-1], position
// h = (n - 1) * p / 100, result x[floor(h)] + (h - floor(h)) *
// (x[floor(h) + 1] - x[floor(h)]), rounded to the nearest cent (halves away
// from zero). This is the "inclusive" method (Excel PERCENTILE.INC, R type 7);
// p = 50 gives the usual median. PLAN 6 fixes the final method in phase 6.
// std::nullopt for an empty list or p outside [0, 100].
std::optional<qint64> percentile(QList<qint64> values, double p);

} // namespace app::lookup
