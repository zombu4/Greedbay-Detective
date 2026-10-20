#include "lookup/History.h"

#include "lookup/Stats.h"
#include "lookup/Text.h"

#include <QSet>

#include <algorithm>

namespace app::lookup {

QString historyKey(const QString &brand, const QString &model, const KnownModels &known)
{
    QString brandKey = brand.simplified().toLower();
    QString modelKey = normalizeCode(model);
    if (const Brand *knownBrand = known.brandByName(brand)) {
        brandKey = knownBrand->id;
        if (const KnownModel *knownModel = known.findModel(knownBrand->id, model))
            modelKey = normalizeCode(knownModel->model);
    }
    return brandKey + QLatin1Char('|') + modelKey;
}

QList<HistoryItem> historyItems(const QList<LookupSummary> &lookups, const KnownModels &known)
{
    QList<HistoryItem> items;
    for (const LookupSummary &lookup : lookups) {
        if (lookup.brand.simplified().isEmpty() || lookup.model.simplified().isEmpty())
            continue;
        const QString key = historyKey(lookup.brand, lookup.model, known);
        auto it = std::find_if(items.begin(), items.end(), [&key](const HistoryItem &item) { return item.key == key; });
        if (it == items.end()) {
            items.append({key, QStringLiteral("%1 %2").arg(lookup.brand.simplified(), lookup.model.simplified()), {}});
            it = items.end() - 1;
        }
        it->checks.append(lookup);
    }
    return items;
}

HistoryPoints historyPoints(const QList<HistorySaleRow> &rows, const QDate &today, int windowDays)
{
    HistoryPoints points;
    const QDate first = today.addDays(-(std::max(windowDays, 1) - 1));
    // Each listing once: its item number, or for rows without one (Product
    // Research rows may show none) its title, date and total.
    QSet<QString> seen;
    for (const HistorySaleRow &row : rows) {
        const QString key = !row.itemId.isEmpty()
                                ? row.itemId
                                : QStringLiteral("%1|%2|%3")
                                      .arg(row.title, row.soldOn.toString(Qt::ISODate),
                                           row.totalCents ? QString::number(*row.totalCents) : QString());
        if (seen.contains(key))
            continue;
        seen.insert(key);
        if (!row.included || !row.totalCents || !row.soldOn.isValid() || row.soldOn < first || row.soldOn > today)
            continue;
        points.sales.append({row.soldOn, *row.totalCents, row.itemId, row.title});
    }
    std::stable_sort(points.sales.begin(), points.sales.end(),
                     [](const HistorySale &a, const HistorySale &b) { return a.soldOn < b.soldOn; });
    QList<qint64> totals;
    for (const HistorySale &sale : std::as_const(points.sales))
        totals.append(sale.totalCents);
    points.medianCents = percentile(totals, 50);
    points.p25Cents = percentile(totals, 25);
    points.p75Cents = percentile(totals, 75);
    return points;
}

} // namespace app::lookup
