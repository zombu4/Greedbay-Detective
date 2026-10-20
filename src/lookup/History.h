#pragma once

#include "lookup/KnownModels.h"
#include "lookup/LookupStore.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace app::lookup {

// Price history (ui-spec 3, DESIGN 3.5): the items the user checked, what
// the same item sold for over time, and the user's own checks of it. Every
// lookup is a snapshot that stays; a re-check adds a new one.

// An item is a brand and a model. Its key: the known brand of the saved
// brand name (any of its names, so "HP" and "Hewlett Packard" are one) and
// the known model's own code (a family member counts as its model), else
// the saved brand and model; both normalized ("heathkit|IM13").
QString historyKey(const QString &brand, const QString &model, const KnownModels &known);

struct HistoryItem {
    QString key;
    QString name;              // "<brand> <model>" as the newest lookup has it
    QList<LookupSummary> checks; // the lookups of the item, newest first
};

// The items of `lookups` (newest first, as LookupStore::recent gives them)
// that have a brand and a model, the most recently checked first.
QList<HistoryItem> historyItems(const QList<LookupSummary> &lookups, const KnownModels &known);

// One sale of the same item.
struct HistorySale {
    QDate soldOn;
    qint64 totalCents = 0;
    QString itemId;
    QString title;
};

// What the chart shows for one window.
struct HistoryPoints {
    QList<HistorySale> sales; // oldest first
    std::optional<qint64> medianCents;
    std::optional<qint64> p25Cents; // the middle half of the sales
    std::optional<qint64> p75Cents;
};

// The sales of an item from the sale rows of its lookups (`rows` as
// LookupStore::historySales gives them, newest lookup first): each listing
// once, as the newest lookup that has it shows it, counted only when that
// lookup counts it (the user's ticks) and it has a total and a sold date;
// only the sales of the last `windowDays` days up to `today`. Median and
// middle half (P25-P75, Stats::percentile) over those sales.
HistoryPoints historyPoints(const QList<HistorySaleRow> &rows, const QDate &today, int windowDays);

} // namespace app::lookup
