#include "ui/ResultsController.h"

#include "ui/Format.h"

#include <QJsonObject>

#include <algorithm>
#include <utility>

namespace app::ui {

using lookup::Bucket;
using lookup::Row;

namespace {

double centsOr(const std::optional<qint64> &cents)
{
    return cents ? static_cast<double>(*cents) : -1.0;
}

QString sourceText(const QString &source)
{
    if (source == QLatin1String("item specifics"))
        return QStringLiteral("from item specifics");
    if (source == QLatin1String("known model"))
        return QStringLiteral("known model");
    if (source == QLatin1String("title"))
        return QStringLiteral("from the title");
    if (source == QLatin1String("user") || source == QLatin1String("saved"))
        return QStringLiteral("you corrected it");
    if (source == QLatin1String("older version"))
        return QStringLiteral("saved by an older version of the app");
    return source;
}

QString numbersLine(const lookup::ValueNumbers &numbers)
{
    QString line = QStringLiteral("%1 - %2 confidence").arg(countText(numbers.included, QStringLiteral("sale"), QStringLiteral("sales")),
                                                           numbers.confidence.toLower());
    if (numbers.medianCents)
        line += QStringLiteral(", median %1, middle half %2 - %3")
                    .arg(moneyText(numbers.medianCents), moneyText(numbers.p25Cents), moneyText(numbers.p75Cents));
    if (!numbers.outliers.isEmpty())
        line += QStringLiteral(", %1 left out").arg(countText(numbers.outliers.size(), QStringLiteral("outlier"), QStringLiteral("outliers")));
    return line;
}

} // namespace

RowsModel::RowsModel(QObject *parent) : QAbstractListModel(parent) {}

void RowsModel::reset(const QList<Entry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
    emit countChanged();
}

void RowsModel::update(const QList<Entry> &entries)
{
    if (entries.size() != m_entries.size()) {
        reset(entries);
        return;
    }
    m_entries = entries;
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(count() - 1), {IncludedRole, MarkRole});
}

int RowsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant RowsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case RowRole:
        return entry.row;
    case IncludedRole:
        return entry.included;
    case WhereRole:
        return entry.where;
    case TotalRole:
        return entry.total;
    case ConditionRole:
        return entry.condition;
    case DateRole:
        return entry.date;
    case TitleRole:
        return entry.title;
    case ItemIdRole:
        return entry.itemId;
    case ReasonRole:
        return entry.reason;
    case MarkRole:
        return entry.mark;
    default:
        return {};
    }
}

QHash<int, QByteArray> RowsModel::roleNames() const
{
    return {{RowRole, "row"},         {IncludedRole, "included"}, {WhereRole, "where"},   {TotalRole, "total"},
            {ConditionRole, "condition"}, {DateRole, "date"},     {TitleRole, "title"},   {ItemIdRole, "itemId"},
            {ReasonRole, "reason"},   {MarkRole, "mark"}};
}

ResultsController::ResultsController(lookup::LookupStore store, std::function<ValueSettings()> valueSettings,
                                     std::function<QString(const QString &)> photoSourceOf, QObject *parent)
    : QObject(parent), m_store(std::move(store)), m_valueSettings(std::move(valueSettings)),
      m_photoSourceOf(std::move(photoSourceOf)), m_sameItem(new RowsModel(this)), m_notThisItem(new RowsModel(this))
{
}

bool ResultsController::show(qint64 lookupId, QString *error)
{
    std::optional<lookup::SavedLookup> saved = m_store.load(lookupId, error);
    if (!saved)
        return false;
    m_saved = std::move(*saved);
    m_storeError.clear();
    m_photoSource = m_photoSourceOf(m_saved.result.item.itemId);
    recompute();
    m_sameItem->reset(entries(true));
    m_notThisItem->reset(entries(false));
    emit changed();
    return true;
}

void ResultsController::photoChanged()
{
    m_photoSource = m_photoSourceOf(m_saved.result.item.itemId);
    emit changed();
}

void ResultsController::setIncluded(int row, bool included)
{
    if (row < 0 || row >= m_saved.rows.size() || m_saved.rows.at(row).included == included)
        return;
    m_saved.rows[row].included = included;
    recompute();
    QString error;
    m_storeError = m_store.updateIncluded(m_saved.id, m_saved.result, m_saved.rows, m_valuation, &error)
                       ? QString()
                       : QStringLiteral("Your choice is shown but could not be saved: %1").arg(error);
    m_sameItem->update(entries(true));
    m_notThisItem->update(entries(false));
    emit changed();
}

void ResultsController::recompute()
{
    const lookup::LookupResult &result = m_saved.result;
    m_valuation = lookup::valuate(result, m_saved.rows, m_valueSettings());

    // CHEAPEST: the lowest totals among the counted for-sale rows other than
    // the pasted listing (the same rows as m_valuation.forSale).
    QList<int> forSale;
    for (int i = 0; i < m_saved.rows.size(); ++i) {
        const Row &row = m_saved.rows.at(i);
        if (row.source == Row::Source::Active && row.included && lookup::rowTotal(result, row)
            && result.active.at(row.index).itemId != result.item.itemId)
            forSale.append(i);
    }
    std::stable_sort(forSale.begin(), forSale.end(), [&](int a, int b) {
        return *lookup::rowTotal(result, m_saved.rows.at(a)) < *lookup::rowTotal(result, m_saved.rows.at(b));
    });
    m_cheapest.clear();
    for (int i = 0; i < forSale.size() && i < lookup::kCheapestMarked; ++i)
        m_cheapest.insert(forSale.at(i));
    m_cheapestItemId.clear();
    m_cheapestTotal.reset();
    if (!forSale.isEmpty()) {
        const Row &cheapest = m_saved.rows.at(forSale.first());
        m_cheapestItemId = result.active.at(cheapest.index).itemId;
        m_cheapestTotal = lookup::rowTotal(result, cheapest);
    }

    const lookup::ValueNumbers &sold = m_valuation.sold;
    m_bar = Bar();
    QList<double> values;
    auto add = [&values](double value) {
        if (value >= 0)
            values.append(value);
        return value;
    };
    m_bar.bandLow = add(centsOr(sold.p25Cents));
    m_bar.bandHigh = add(centsOr(sold.p75Cents));
    m_bar.median = add(centsOr(sold.medianCents));
    add(centsOr(sold.lowCents));
    add(centsOr(sold.highCents));
    m_bar.thisTotal = add(centsOr(result.item.totalCents()));
    m_bar.cheapest = add(centsOr(m_cheapestTotal));
    if (!values.isEmpty() && sold.medianCents) {
        m_bar.min = *std::min_element(values.cbegin(), values.cend());
        m_bar.max = *std::max_element(values.cbegin(), values.cend());
        // A bar needs a width: one dollar either side when everything is
        // one amount.
        if (m_bar.max - m_bar.min < 1.0) {
            m_bar.min = std::max(0.0, m_bar.min - 100.0);
            m_bar.max += 100.0;
        }
    } else {
        m_bar = Bar();
    }
}

QList<RowsModel::Entry> ResultsController::entries(bool sameItem) const
{
    const lookup::LookupResult &result = m_saved.result;
    QList<int> order;
    for (int i = 0; i < m_saved.rows.size(); ++i) {
        if ((m_saved.rows.at(i).match.bucket == Bucket::SameItem) == sameItem)
            order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const std::optional<qint64> totalA = lookup::rowTotal(result, m_saved.rows.at(a));
        const std::optional<qint64> totalB = lookup::rowTotal(result, m_saved.rows.at(b));
        if (totalA.has_value() != totalB.has_value())
            return totalA.has_value();
        return totalA && *totalA < *totalB;
    });
    QList<RowsModel::Entry> list;
    for (const int i : order) {
        const Row &row = m_saved.rows.at(i);
        RowsModel::Entry entry;
        entry.row = i;
        entry.included = row.included;
        entry.total = moneyText(lookup::rowTotal(result, row));
        entry.condition = lookup::rowCondition(result, row);
        entry.title = lookup::rowTitle(result, row);
        if (row.source == Row::Source::Research) {
            const lookup::ResearchRecord &record = result.research.at(row.index);
            entry.where = QStringLiteral("research");
            entry.date = record.lastSoldDate.isEmpty() ? QStringLiteral("-") : record.lastSoldDate;
            entry.itemId = record.itemId;
        } else {
            const bool soldRow = row.source == Row::Source::Sold;
            const lookup::SearchRecord &record = soldRow ? result.sold.at(row.index) : result.active.at(row.index);
            entry.where = soldRow ? (record.confirmed ? QStringLiteral("sold") : QStringLiteral("sold, price not confirmed"))
                                  : QStringLiteral("for sale");
            entry.date = soldRow && !record.soldDate.isEmpty() ? record.soldDate : QStringLiteral("-");
            entry.itemId = record.itemId;
            if (!soldRow && record.itemId == result.item.itemId)
                entry.mark = QStringLiteral("this");
            else if (m_cheapest.contains(i))
                entry.mark = QStringLiteral("cheapest");
        }
        if (row.match.bucket != Bucket::SameItem) {
            entry.reason = lookup::bucketName(row.match.bucket);
            if (!row.match.reasons.isEmpty())
                entry.reason += QStringLiteral(": ") + row.match.reasons.join(QStringLiteral("; "));
        }
        list.append(entry);
    }
    return list;
}

QString ResultsController::statusText() const
{
    const lookup::LookupResult &result = m_saved.result;
    QString status;
    if (result.status == QLatin1String("cancelled"))
        status = QStringLiteral("Cancelled before the end - the numbers use what was read until then. ");
    else if (result.status == QLatin1String("stopped"))
        status = QStringLiteral("Stopped before the end: %1. The numbers use what was read until then. ").arg(result.message);
    return status
           + QStringLiteral("Saved %1 · %2 · %3 · not loaded again from eBay")
                 .arg(whenText(m_saved.createdAt, QDateTime::currentDateTime()),
                      countText(result.pagesLoaded, QStringLiteral("eBay page"), QStringLiteral("eBay pages")),
                      QStringLiteral("%1 s").arg(QString::number(static_cast<double>(result.elapsedMs) / 1000.0, 'f', 0)));
}

QString ResultsController::statusKind() const
{
    const QString &status = m_saved.result.status;
    if (status == QLatin1String("complete"))
        return QStringLiteral("complete");
    return status == QLatin1String("cancelled") ? QStringLiteral("partial") : QStringLiteral("failed");
}

QString ResultsController::identityName() const
{
    const lookup::Identity &identity = m_saved.result.identity;
    const QString name = QStringLiteral("%1 %2").arg(identity.brandName, identity.model).simplified();
    return name.isEmpty() ? QStringLiteral("an unknown item") : name;
}

QString ResultsController::identitySource() const
{
    const lookup::Identity &identity = m_saved.result.identity;
    if (!identity.confident)
        return identity.reason.isEmpty() ? QStringLiteral("not confirmed")
                                         : QStringLiteral("not confirmed: %1").arg(identity.reason);
    return sourceText(identity.source);
}

QString ResultsController::searches() const
{
    return m_saved.result.queries.isEmpty() ? QStringLiteral("nothing was searched")
                                            : m_saved.result.queries.join(QStringLiteral("  |  "));
}

QStringList ResultsController::identityNotes() const
{
    const lookup::Identity &identity = m_saved.result.identity;
    QStringList notes;
    if (!identity.family.isEmpty())
        notes.append(QStringLiteral("Counts as the same item: %1").arg(identity.family.join(QStringLiteral(", "))));
    for (const QString &note : identity.notes)
        notes.append(QStringLiteral("Known issue: %1").arg(note));
    return notes;
}

QString ResultsController::priceText() const { return moneyText(m_saved.result.item.priceCents); }
QString ResultsController::shippingText() const { return moneyText(m_saved.result.item.shippingCents); }
QString ResultsController::totalText() const { return moneyText(m_saved.result.item.totalCents()); }

QStringList ResultsController::shippingLines() const
{
    const lookup::ShippingCheck &shipping = m_valuation.shipping;
    if (!shipping.flagged)
        return {};
    QStringList lines;
    auto against = [&](const QString &what, qint64 reference) {
        if (reference > 0)
            return QStringLiteral("%1 is %2x %3 (%4)")
                .arg(moneyText(shipping.listingCents),
                     QString::number(static_cast<double>(*shipping.listingCents) / static_cast<double>(reference), 'f', 1),
                     what, moneyText(reference));
        return QStringLiteral("%1, where %2 is %3").arg(moneyText(shipping.listingCents), what, moneyText(reference));
    };
    if (shipping.flaggedVsEbay)
        lines.append(against(QStringLiteral("eBay's average for this item"), *shipping.ebayAverageCents));
    if (shipping.flaggedVsComps)
        lines.append(against(QStringLiteral("the median shipping of the matched sales"), *shipping.compMedianCents));
    if (const std::optional<qint64> total = m_saved.result.item.totalCents(); total && *total > 0)
        lines.append(QStringLiteral("Shipping is %1% of this listing's total")
                         .arg(QString::number(100.0 * static_cast<double>(*shipping.listingCents) / static_cast<double>(*total), 'f', 0)));
    return lines;
}

bool ResultsController::cheaperFlag() const
{
    return m_valuation.cheaperForSale > 0 && m_cheapestTotal.has_value();
}

QString ResultsController::cheaperLine() const
{
    if (!cheaperFlag())
        return {};
    const int count = m_valuation.cheaperForSale;
    return QStringLiteral("%1 of the same item %2 less: %3 total")
        .arg(countText(count, QStringLiteral("listing"), QStringLiteral("listings")),
             count == 1 ? QStringLiteral("costs") : QStringLiteral("cost"), moneyText(m_cheapestTotal));
}

QString ResultsController::soldLine() const
{
    const lookup::ValueNumbers &sold = m_valuation.sold;
    QString line = QStringLiteral("Sold search, totals incl. shipping: %1 - %2 confidence")
                       .arg(countText(sold.included, QStringLiteral("sale"), QStringLiteral("sales")), sold.confidence.toLower());
    if (!sold.outliers.isEmpty())
        line += QStringLiteral(", %1 left out").arg(countText(sold.outliers.size(), QStringLiteral("outlier"), QStringLiteral("outliers")));
    if (sold.unconfirmed > 0)
        line += QStringLiteral(", %1 with an unconfirmed Best Offer price not counted (their median %2)")
                    .arg(countText(sold.unconfirmed, QStringLiteral("sale"), QStringLiteral("sales")), moneyText(sold.unconfirmedMedianCents));
    return line;
}

QString ResultsController::medianText() const { return moneyText(m_valuation.sold.medianCents); }

QString ResultsController::middleText() const
{
    const lookup::ValueNumbers &sold = m_valuation.sold;
    return sold.p25Cents ? QStringLiteral("%1 - %2").arg(moneyText(sold.p25Cents), moneyText(sold.p75Cents)) : QStringLiteral("-");
}

QString ResultsController::lowHighText() const
{
    const lookup::ValueNumbers &sold = m_valuation.sold;
    return sold.lowCents ? QStringLiteral("%1 / %2").arg(moneyText(sold.lowCents), moneyText(sold.highCents)) : QStringLiteral("-");
}

QString ResultsController::forSaleText() const
{
    const QList<qint64> &forSale = m_valuation.forSale;
    if (forSale.isEmpty())
        return QStringLiteral("none found");
    return QStringLiteral("%1, from %2").arg(forSale.size()).arg(lookup::money(forSale.first()));
}

QString ResultsController::cheapestLabel() const
{
    return m_cheapestTotal ? QStringLiteral("cheapest for sale %1").arg(moneyText(m_cheapestTotal)) : QString();
}

QStringList ResultsController::sideLines() const
{
    const lookup::LookupResult &result = m_saved.result;
    QStringList lines;
    const QString period = QStringLiteral("last %1").arg(countText(m_saved.historyDays, QStringLiteral("day"), QStringLiteral("days")));
    bool anyResearch = false;
    for (const Row &row : m_saved.rows)
        anyResearch = anyResearch || row.source == Row::Source::Research;
    if (result.researchUnavailable)
        lines.append(QStringLiteral("Product Research is not available on this eBay account, so every sold price is "
                                    "shown as unconfirmed."));
    else if (m_valuation.research.included > 0 || !m_valuation.research.outliers.isEmpty())
        lines.append(QStringLiteral("Product Research (%1, one value per listing: average sold + average shipping): %2")
                         .arg(period, numbersLine(m_valuation.research)));
    else if (anyResearch)
        lines.append(QStringLiteral("Product Research (%1): no listing of the same item counted.").arg(period));
    else if (m_saved.depths.research > 0 && result.status == QLatin1String("complete"))
        lines.append(QStringLiteral("Product Research (%1): no listings found for these searches.").arg(period));
    if (m_valuation.ebayAverageSoldCents) {
        const QJsonValue shipping = result.researchSummary.value(QStringLiteral("avgShippingCents"));
        lines.append(QStringLiteral("eBay's own Product Research average (it may mix in other models): %1 sold%2")
                         .arg(moneyText(m_valuation.ebayAverageSoldCents),
                              shipping.isDouble() ? QStringLiteral(", %1 shipping").arg(moneyText(static_cast<qint64>(shipping.toDouble())))
                                                  : QString()));
    }
    lines.append(QStringLiteral("Shipping costs eBay shows can depend on the ship-to location eBay has for the app's browser."));
    for (const QString &note : result.notes)
        lines.append(note);
    return lines;
}

} // namespace app::ui
