#include "lookup/Stats.h"

#include <algorithm>
#include <cmath>

namespace app::lookup {

std::optional<qint64> percentile(QList<qint64> values, double p)
{
    if (values.isEmpty() || !(p >= 0.0 && p <= 100.0))
        return std::nullopt;
    std::sort(values.begin(), values.end());
    const double h = static_cast<double>(values.size() - 1) * p / 100.0;
    const auto low = static_cast<qsizetype>(std::floor(h));
    const qsizetype high = std::min<qsizetype>(low + 1, values.size() - 1);
    const double value = static_cast<double>(values.at(low))
                         + (h - static_cast<double>(low)) * static_cast<double>(values.at(high) - values.at(low));
    return static_cast<qint64>(std::llround(value));
}

} // namespace app::lookup
