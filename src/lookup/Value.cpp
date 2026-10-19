#include "lookup/Value.h"

#include "lookup/Stats.h"

namespace app::lookup {

QString confidenceName(int count, const ValueSettings &settings)
{
    if (count >= settings.confidenceHigh)
        return QStringLiteral("High");
    if (count >= settings.confidenceMedium)
        return QStringLiteral("Medium");
    if (count >= settings.confidenceLow)
        return QStringLiteral("Low");
    return QStringLiteral("Not enough data");
}

QList<ConditionNumbers> valueByCondition(const QList<Comp> &comps, const ValueSettings &settings)
{
    const Condition order[] = {Condition::New, Condition::Working, Condition::NotStated, Condition::Untested,
                               Condition::NonWorking};
    QList<ConditionNumbers> result;
    for (const Condition condition : order) {
        QList<qint64> confirmed;
        QList<qint64> unconfirmed;
        for (const Comp &comp : comps) {
            if (comp.condition != condition || !comp.totalCents)
                continue;
            (comp.confirmed ? confirmed : unconfirmed).append(*comp.totalCents);
        }
        if (confirmed.isEmpty() && unconfirmed.isEmpty())
            continue;
        ConditionNumbers numbers;
        numbers.condition = condition;
        QList<qint64> kept = confirmed;
        if (!confirmed.isEmpty()) {
            const double q1 = static_cast<double>(*percentile(confirmed, 25));
            const double q3 = static_cast<double>(*percentile(confirmed, 75));
            const double reach = settings.outlierIqrFactor * (q3 - q1);
            kept.clear();
            for (const qint64 total : std::as_const(confirmed)) {
                const double value = static_cast<double>(total);
                (value < q1 - reach || value > q3 + reach ? numbers.outliers : kept).append(total);
            }
        }
        numbers.included = static_cast<int>(kept.size());
        numbers.medianCents = percentile(kept, 50);
        numbers.p25Cents = percentile(kept, 25);
        numbers.p75Cents = percentile(kept, 75);
        numbers.confidence = confidenceName(numbers.included, settings);
        numbers.unconfirmed = static_cast<int>(unconfirmed.size());
        numbers.unconfirmedMedianCents = percentile(unconfirmed, 50);
        result.append(numbers);
    }
    return result;
}

ShippingCheck checkShipping(std::optional<qint64> listingCents, const QList<Comp> &comps,
                            std::optional<qint64> ebayAverageCents, const ValueSettings &settings)
{
    ShippingCheck check;
    check.listingCents = listingCents;
    check.ebayAverageCents = ebayAverageCents;
    QList<qint64> shipping;
    for (const Comp &comp : comps) {
        if (comp.confirmed && comp.shippingCents)
            shipping.append(*comp.shippingCents);
    }
    check.compsWithShipping = static_cast<int>(shipping.size());
    check.compMedianCents = percentile(shipping, 50);
    auto tooHigh = [&](std::optional<qint64> reference) {
        return listingCents && reference
               && static_cast<double>(*listingCents) > settings.shippingFlagRatio * static_cast<double>(*reference)
               && *listingCents - *reference > settings.shippingFlagMinAboveCents;
    };
    check.flaggedVsComps = tooHigh(check.compMedianCents);
    check.flaggedVsEbay = tooHigh(ebayAverageCents);
    check.flagged = check.flaggedVsComps || check.flaggedVsEbay;
    return check;
}

} // namespace app::lookup
