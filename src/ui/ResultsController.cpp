#include "ui/ResultsController.h"

#include "core/ExportFiles.h"
#include "lookup/AuctionTime.h"
#include "lookup/LookupExport.h"
#include "plugins/PluginHost.h"
#include "ui/Format.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTimeZone>

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

// "high confidence", "not enough data" (+ ", 1 outlier left out").
QString confidenceText(const lookup::ValueNumbers &numbers)
{
    QString text = numbers.confidence.toLower();
    if (text != QLatin1String("not enough data"))
        text += QStringLiteral(" confidence");
    if (!numbers.outliers.isEmpty())
        text += QStringLiteral(", %1 left out").arg(countText(numbers.outliers.size(), QStringLiteral("outlier"), QStringLiteral("outliers")));
    return text;
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
        emit dataChanged(index(0), index(count() - 1), {IncludedRole, MarkRole, SellerBadgeRole, SellerMarkedRole, SellerSharedRole});
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
    case BrokenRole:
        return entry.broken;
    case FormatRole:
        return entry.format;
    case TimeLeftRole:
        return entry.timeLeft;
    case ViewRole:
        return entry.view;
    case ViewNoteRole:
        return entry.viewNote;
    case SellerRole:
        return entry.seller;
    case SellerBadgeRole:
        return entry.sellerBadge;
    case SellerMarkedRole:
        return entry.sellerMarked;
    case SellerSharedRole:
        return entry.sellerShared;
    case SellerPercentRole:
        return entry.sellerPercent;
    case SellerCountRole:
        return entry.sellerCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> RowsModel::roleNames() const
{
    return {{RowRole, "row"},         {IncludedRole, "included"}, {WhereRole, "where"},   {TotalRole, "total"},
            {ConditionRole, "condition"}, {DateRole, "date"},     {TitleRole, "title"},   {ItemIdRole, "itemId"},
            {ReasonRole, "reason"},   {MarkRole, "mark"},       {BrokenRole, "broken"},
            {FormatRole, "format"}, {TimeLeftRole, "timeLeft"}, {ViewRole, "view"},   {ViewNoteRole, "viewNote"},
            {SellerRole, "seller"}, {SellerBadgeRole, "sellerBadge"}, {SellerMarkedRole, "sellerMarked"},
            {SellerSharedRole, "sellerShared"},
            {SellerPercentRole, "sellerPercent"}, {SellerCountRole, "sellerCount"}};
}

ResultsController::ResultsController(lookup::LookupStore store, lookup::SellerStore sellers, const lookup::MatchWords *words,
                                     std::function<ValueSettings()> valueSettings,
                                     std::function<lookup::SellerRules()> sellerRules,
                                     std::function<QString(const QString &)> photoSourceOf, QObject *parent)
    : QObject(parent), m_store(std::move(store)), m_words(words), m_valueSettings(std::move(valueSettings)),
      m_sellerRules(std::move(sellerRules)), m_photoSourceOf(std::move(photoSourceOf)), m_sellers(std::move(sellers)),
      m_sameItem(new RowsModel(this)), m_notThisItem(new RowsModel(this))
{
}

void ResultsController::loadSellerMarks()
{
    m_marks.clear();
    QString error;
    if (const std::optional<QList<lookup::SellerMark>> marks = m_sellers.marks(&error)) {
        for (const lookup::SellerMark &mark : *marks)
            m_marks.insert(mark.sellerId.toLower(), mark);
    } else {
        qWarning().noquote() << "results: the marked sellers cannot be read:" << error;
    }
}

void ResultsController::refreshSellerMarks()
{
    loadSellerMarks();
    if (!m_saved.result.item.itemId.isEmpty()) {
        m_sameItem->update(entries(true));
        m_notThisItem->update(entries(false));
    }
    emit changed();
}

void ResultsController::setSharedReports(const QHash<QString, QStringList> &linesBySeller)
{
    m_shared = linesBySeller;
    if (!m_saved.result.item.itemId.isEmpty()) {
        m_sameItem->update(entries(true));
        m_notThisItem->update(entries(false));
    }
    emit changed();
}

bool ResultsController::sharedFlag() const { return !sellerId().isEmpty() && m_shared.contains(sellerId().toLower()); }

QStringList ResultsController::sharedLines() const { return sharedFlag() ? m_shared.value(sellerId().toLower()) : QStringList(); }

bool ResultsController::markedFlag() const
{
    return !sellerId().isEmpty() && m_marks.contains(sellerId().toLower());
}

QStringList ResultsController::markedLines() const
{
    if (!markedFlag())
        return {};
    const lookup::SellerMark mark = m_marks.value(sellerId().toLower());
    QStringList lines{QStringLiteral("You marked %1 on %2: %3").arg(mark.sellerId, mark.markedOn.toString(Qt::ISODate),
                                                                     lookup::markReasonLabel(mark.reason))};
    if (!mark.note.isEmpty())
        lines.append(mark.note);
    return lines;
}

bool ResultsController::ratingFlag() const
{
    const lookup::SellerRules rules = m_sellerRules();
    return lookup::ratingNote(m_saved.result.item.sellerPositivePercent, m_saved.result.item.sellerFeedbackCount,
                              rules.threshold, rules.fewRatings).flagged;
}

QString ResultsController::ratingLine() const
{
    const lookup::SellerRules rules = m_sellerRules();
    return lookup::ratingNote(m_saved.result.item.sellerPositivePercent, m_saved.result.item.sellerFeedbackCount,
                              rules.threshold, rules.fewRatings).text;
}

bool ResultsController::show(qint64 lookupId, QString *error)
{
    std::optional<lookup::SavedLookup> saved = m_store.load(lookupId, error);
    if (!saved)
        return false;
    m_saved = std::move(*saved);
    m_storeError.clear();
    m_photoSource = m_photoSourceOf(m_saved.result.item.itemId);
    m_pluginNotices.clear();
    loadSellerMarks();
    recompute();
    runPlugins();
    m_sameItem->reset(entries(true));
    m_notThisItem->reset(entries(false));
    emit changed();
    return true;
}

void ResultsController::runPlugins()
{
    m_pluginFlags.clear();
    m_pluginValues.clear();
    m_exporters.clear();
    if (!m_plugins || !hasLookup())
        return;
    for (const plugins::PluginFlag &flag : m_plugins->flags(m_saved.result, m_saved.rows)) {
        m_pluginFlags.append(QVariantMap{{QStringLiteral("plugin"), flag.pluginName},
                                         {QStringLiteral("text"), flag.text},
                                         {QStringLiteral("kind"), flag.kind}});
    }
    for (const plugins::PluginValue &value : m_plugins->values(m_saved.result, m_saved.rows)) {
        m_pluginValues.append(QVariantMap{{QStringLiteral("plugin"), value.pluginName},
                                          {QStringLiteral("label"), value.label},
                                          {QStringLiteral("value"), moneyText(value.cents)},
                                          {QStringLiteral("note"), value.note}});
    }
    for (const plugins::PluginManifest &exporter : m_plugins->exporters())
        m_exporters.append(QVariantMap{{QStringLiteral("id"), exporter.id}, {QStringLiteral("name"), exporter.name}});
    m_pluginNotices.append(m_plugins->takeFailures());
}

QVariantMap ResultsController::saveFile(const QString &fileName, const QByteArray &data) const
{
    QString error;
    const std::optional<QString> path = saveExport(exportFolder(), fileName, data, &error);
    if (!path)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("Not saved: %1").arg(error)}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("message"), QStringLiteral("Saved as %1").arg(QDir::toNativeSeparators(*path))},
            {QStringLiteral("path"), *path}};
}

QVariantMap ResultsController::saveJson()
{
    if (!hasLookup())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("No lookup is shown.")}};
    const QString name = m_saved.result.item.itemId.isEmpty() ? QString::number(m_saved.id) : m_saved.result.item.itemId;
    return saveFile(QStringLiteral("lookup-%1-%2.json").arg(name, m_saved.createdAt.toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
                    QJsonDocument(lookup::lookupJson(m_saved)).toJson(QJsonDocument::Indented));
}

QVariantMap ResultsController::exportWith(const QString &pluginId)
{
    if (!hasLookup() || !m_plugins)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("No lookup is shown.")}};
    QString error;
    const std::optional<plugins::PluginExport> file = m_plugins->exportLookup(pluginId, m_saved.result, m_saved.rows, &error);
    if (!file) {
        runPlugins();
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("Not saved: %1").arg(error)}};
    }
    return saveFile(file->fileName, file->data);
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
    runPlugins();
    QString error;
    m_storeError = m_store.updateIncluded(m_saved.id, m_saved.result, m_saved.rows, m_valuation, &error)
                       ? QString()
                       : QStringLiteral("Your choice is shown but could not be saved: %1").arg(error);
    m_sameItem->update(entries(true));
    m_notThisItem->update(entries(false));
    emit changed();
    if (m_storeError.isEmpty())
        emit stored();
}

void ResultsController::recompute()
{
    const lookup::LookupResult &result = m_saved.result;
    m_valuation = lookup::valuate(result, m_saved.rows, m_valueSettings());

    // CHEAPEST: the lowest totals among the counted Buy It Now rows other
    // than the pasted listing (the same rows as m_valuation.forSale).
    QList<int> forSale;
    for (int i = 0; i < m_saved.rows.size(); ++i) {
        const Row &row = m_saved.rows.at(i);
        if (row.source == Row::Source::Active && row.included && lookup::rowTotal(result, row)
            && result.active.at(row.index).itemId != result.item.itemId
            && result.active.at(row.index).format == QLatin1String("fixed_price"))
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

    // The sold search's numbers, or Product Research's when the sold search
    // counted none.
    const lookup::ValueNumbers &sold = m_valuation.sold.medianCents ? m_valuation.sold : m_valuation.research;
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
    // The settings once per table (they are read from the settings file).
    const lookup::SellerRules rules = m_sellerRules();
    QList<RowsModel::Entry> list;
    for (const int i : order) {
        const Row &row = m_saved.rows.at(i);
        RowsModel::Entry entry;
        entry.row = i;
        entry.included = row.included;
        entry.total = moneyText(lookup::rowTotal(result, row));
        entry.condition = lookup::rowCondition(result, row);
        entry.title = lookup::rowTitle(result, row);
        if (m_words)
            entry.broken = lookup::likelyBrokenReason(entry.title, entry.condition, *m_words);
        if (row.source == Row::Source::Research) {
            const lookup::ResearchRecord &record = result.research.at(row.index);
            entry.where = QStringLiteral("research");
            entry.date = record.lastSoldDate.isEmpty() ? QStringLiteral("-") : record.lastSoldDate;
            entry.itemId = record.itemId;
        } else {
            const bool soldRow = row.source == Row::Source::Sold;
            const lookup::SearchRecord &record = soldRow ? result.sold.at(row.index) : result.active.at(row.index);
            entry.seller = record.sellerName;
            entry.sellerPercent = record.sellerPositivePercent.value_or(-1);
            entry.sellerCount = record.sellerFeedbackCount.value_or(-1);
            entry.sellerMarked = !record.sellerName.isEmpty() && m_marks.contains(record.sellerName.toLower());
            entry.sellerShared = record.sellerName.isEmpty() ? 0 : static_cast<int>(m_shared.value(record.sellerName.toLower()).size());
            entry.sellerBadge = lookup::ratingNote(record.sellerPositivePercent, record.sellerFeedbackCount, rules.threshold,
                                                   rules.fewRatings).badge;
            entry.where = soldRow ? (record.confirmed ? QStringLiteral("sold") : QStringLiteral("sold, price not confirmed"))
                                  : QStringLiteral("for sale");
            entry.date = soldRow && !record.soldDate.isEmpty() ? record.soldDate : QStringLiteral("-");
            entry.itemId = record.itemId;
            if (!soldRow && record.itemId == result.item.itemId)
                entry.mark = QStringLiteral("this");
            else if (m_cheapest.contains(i))
                entry.mark = QStringLiteral("cheapest");
        }
        if (row.source == Row::Source::Active) {
            const lookup::SearchRecord &record = result.active.at(row.index);
            const QDateTime now = QDateTime::currentDateTime();
            if (record.format == QLatin1String("auction")) {
                entry.format = record.bids ? QStringLiteral("AUCTION - %1").arg(countText(*record.bids, QStringLiteral("bid"), QStringLiteral("bids")))
                                           : QStringLiteral("AUCTION");
                entry.timeLeft = record.auctionEnd ? lookup::timeLeftText(*record.auctionEnd, record.auctionEndSeconds, now)
                                 : record.timeLeft.isEmpty() ? QString()
                                                             : QStringLiteral("%1 (when checked)").arg(record.timeLeft);
                // An auction whose known end has passed is no longer for sale.
                entry.view = !(record.auctionEnd && *record.auctionEnd <= now);
            } else {
                if (record.format == QLatin1String("fixed_price"))
                    entry.format = record.bestOffer ? QStringLiteral("BEST OFFER") : QStringLiteral("BUY IT NOW");
                entry.view = true;
                entry.viewNote = QStringLiteral("listed when checked %1").arg(whenText(m_saved.createdAt, now));
            }
            entry.view = entry.view && !record.itemId.isEmpty();
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
    const QString saved = whenText(m_saved.createdAt, QDateTime::currentDateTime());
    // Version 1 recorded neither the pages nor the time of a lookup.
    if (result.identity.source == QLatin1String("older version"))
        return status + QStringLiteral("Saved %1 by an older version of the app · not loaded again from eBay").arg(saved);
    return status
           + QStringLiteral("Saved %1 · %2 · %3 · not loaded again from eBay")
                 .arg(saved, countText(result.pagesLoaded, QStringLiteral("eBay page"), QStringLiteral("eBay pages")),
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

QString ResultsController::identityWarning() const
{
    const QString note = QString::fromLatin1(lookup::kIdentityMismatch);
    return m_saved.result.notes.contains(note) ? note : QString();
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

QString ResultsController::brokenReason() const
{
    if (!m_words || !hasLookup())
        return {};
    const lookup::ItemRecord &item = m_saved.result.item;
    return lookup::likelyBrokenReason(item.title, item.condition, *m_words);
}

QString ResultsController::brokenLine() const
{
    const QString reason = brokenReason();
    if (reason.isEmpty())
        return {};
    return QStringLiteral("Likely broken: %1. Sellers often don't test or don't say. Assume it does not work.").arg(reason);
}

QString ResultsController::formatText() const
{
    const lookup::ItemRecord &item = m_saved.result.item;
    if (item.auction) {
        const QString bids = item.bids ? countText(*item.bids, QStringLiteral("bid"), QStringLiteral("bids")) : QStringLiteral("- bids");
        return QStringLiteral("Auction - %1, ends %2").arg(bids, auctionEndText());
    }
    if (item.buyItNow)
        return item.bestOffer ? QStringLiteral("Buy It Now or Best Offer") : QStringLiteral("Buy It Now");
    return item.bestOffer ? QStringLiteral("Best Offer") : QStringLiteral("-");
}

double ResultsController::auctionEndMs() const
{
    const std::optional<QDateTime> &end = m_saved.result.item.auctionEnd;
    return end ? static_cast<double>(end->toMSecsSinceEpoch()) : -1.0;
}

QString ResultsController::auctionEndText() const
{
    const std::optional<QDateTime> &end = m_saved.result.item.auctionEnd;
    if (!end)
        return QStringLiteral("-");
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    return english.toString(end->toLocalTime(), QStringLiteral("ddd MMM d, h:mm AP")) + QStringLiteral(" (read ")
           + whenText(m_saved.createdAt, QDateTime::currentDateTime()) + QLatin1Char(')');
}

QString ResultsController::timeLeft(double endMs, bool seconds) const
{
    if (endMs < 0)
        return QStringLiteral("-");
    return lookup::timeLeftText(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(endMs), QTimeZone::UTC), seconds,
                                QDateTime::currentDateTimeUtc());
}

QString ResultsController::auctionLabel() const
{
    if (!isAuction())
        return {};
    const lookup::ItemRecord &item = m_saved.result.item;
    const lookup::ValueNumbers &sold = m_valuation.sold.medianCents ? m_valuation.sold : m_valuation.research;
    if (item.bids && *item.bids == 0 && item.priceCents && sold.p75Cents && *item.priceCents > *sold.p75Cents)
        return QStringLiteral("AUCTION STARTS HIGH");
    return QStringLiteral("AUCTION");
}

QStringList ResultsController::auctionLines() const
{
    if (!isAuction())
        return {};
    const lookup::ItemRecord &item = m_saved.result.item;
    const lookup::ValueNumbers &sold = m_valuation.sold.medianCents ? m_valuation.sold : m_valuation.research;
    if (item.bids && *item.bids > 0)
        return {QStringLiteral("The current bid %1 is not the final price - it will likely go higher. Compare the sold "
                               "numbers, not the current bid.")
                    .arg(moneyText(item.priceCents))};
    if (auctionLabel() == QLatin1String("AUCTION STARTS HIGH"))
        return {QStringLiteral("The starting price %1 is already above what the same item sold for (median %2).")
                    .arg(moneyText(item.priceCents), moneyText(sold.medianCents))};
    if (item.bids && *item.bids == 0)
        return {QStringLiteral("Starts at %1 - the final price is decided by bidders.").arg(moneyText(item.priceCents))};
    return {QStringLiteral("The price %1 is a bid, not a final price; eBay showed no bid count.").arg(moneyText(item.priceCents))};
}

QString ResultsController::thisLabel() const
{
    const lookup::ItemRecord &item = m_saved.result.item;
    if (item.auction && item.bids && *item.bids > 0)
        return QStringLiteral("current bid %1").arg(moneyText(item.priceCents));
    return QStringLiteral("THIS ") + totalText();
}

std::pair<int, std::optional<qint64>> ResultsController::runningAuctions() const
{
    // The counted auctions (m_valuation.auctions' rows) whose stored end has
    // not passed; one without a known end is taken as running.
    const lookup::LookupResult &result = m_saved.result;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int running = 0;
    std::optional<qint64> lowest;
    for (const Row &row : m_saved.rows) {
        if (row.source != Row::Source::Active || !row.included)
            continue;
        const lookup::SearchRecord &record = result.active.at(row.index);
        if (record.itemId == result.item.itemId || record.format != QLatin1String("auction") || !record.priceCents
            || (record.auctionEnd && *record.auctionEnd <= now))
            continue;
        ++running;
        if (!lowest || *record.priceCents < *lowest)
            lowest = record.priceCents;
    }
    return {running, lowest};
}

QString ResultsController::auctionsCountText() const
{
    return QString::number(runningAuctions().first);
}

QString ResultsController::lowestBidText() const
{
    return moneyText(runningAuctions().second);
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

QString ResultsController::soldCountText() const
{
    return countText(m_valuation.sold.included, QStringLiteral("sale"), QStringLiteral("sales"));
}

QString ResultsController::soldConfidenceText() const
{
    return confidenceText(m_valuation.sold);
}

QString ResultsController::soldBestOfferNote() const
{
    const int count = m_valuation.sold.unconfirmed;
    if (count == 0)
        return {};
    return QStringLiteral("%1 not counted (real price unknown)")
        .arg(countText(count, QStringLiteral("Best Offer sale"), QStringLiteral("Best Offer sales")));
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

QString ResultsController::binCountText() const
{
    return QString::number(m_valuation.forSale.size());
}

QString ResultsController::cheapestBinText() const
{
    return m_valuation.forSale.isEmpty() ? QStringLiteral("-") : lookup::money(m_valuation.forSale.first());
}

QString ResultsController::forSaleFootnote() const
{
    return QStringLiteral("Shipping costs eBay shows can depend on the ship-to location eBay has for the app's browser.");
}

QString ResultsController::cheapestLabel() const
{
    return m_cheapestTotal ? QStringLiteral("cheapest for sale %1").arg(moneyText(m_cheapestTotal)) : QString();
}

QString ResultsController::periodText() const
{
    const int days = m_saved.historyDays;
    if (days > 0 && days % 365 == 0)
        return QStringLiteral("last %1").arg(countText(days / 365, QStringLiteral("year"), QStringLiteral("years")));
    return QStringLiteral("last %1").arg(countText(days, QStringLiteral("day"), QStringLiteral("days")));
}

QString ResultsController::researchHeading() const
{
    return QStringLiteral("Sold on eBay - %1 (Product Research)").arg(periodText());
}

QString ResultsController::researchStatus() const
{
    const lookup::LookupResult &result = m_saved.result;
    if (result.researchUnavailable)
        return QStringLiteral("Product Research is not available on this eBay account, so every sold price is shown as "
                              "unconfirmed.");
    const lookup::ValueNumbers &research = m_valuation.research;
    if (research.included > 0 || !research.outliers.isEmpty())
        return {};
    bool anyResearch = false;
    for (const Row &row : m_saved.rows)
        anyResearch = anyResearch || row.source == Row::Source::Research;
    if (anyResearch)
        return QStringLiteral("No listing of the same item counted.");
    if (m_saved.depths.research > 0 && result.status == QLatin1String("complete"))
        return QStringLiteral("No listings found for these searches.");
    return QStringLiteral("Not read.");
}

QString ResultsController::researchCountText() const
{
    return countText(m_valuation.research.included, QStringLiteral("listing"), QStringLiteral("listings"));
}

QString ResultsController::researchConfidenceText() const
{
    return confidenceText(m_valuation.research);
}

QString ResultsController::researchFootnote() const
{
    if (!m_valuation.ebayAverageSoldCents)
        return {};
    const QJsonValue shipping = m_saved.result.researchSummary.value(QStringLiteral("avgShippingCents"));
    return QStringLiteral("eBay's own Product Research average: %1 sold%2 (may mix in other models)")
        .arg(moneyText(m_valuation.ebayAverageSoldCents),
             shipping.isDouble() ? QStringLiteral(", %1 shipping").arg(moneyText(static_cast<qint64>(shipping.toDouble())))
                                 : QString());
}

QString ResultsController::researchMedianText() const { return moneyText(m_valuation.research.medianCents); }

QString ResultsController::researchMiddleText() const
{
    const lookup::ValueNumbers &research = m_valuation.research;
    return research.p25Cents ? QStringLiteral("%1 - %2").arg(moneyText(research.p25Cents), moneyText(research.p75Cents))
                             : QStringLiteral("-");
}

QString ResultsController::researchLowHighText() const
{
    const lookup::ValueNumbers &research = m_valuation.research;
    return research.lowCents ? QStringLiteral("%1 / %2").arg(moneyText(research.lowCents), moneyText(research.highCents))
                             : QStringLiteral("-");
}

QString ResultsController::bandLegend() const
{
    if (m_valuation.sold.medianCents)
        return QStringLiteral("middle half of sold (last 90 days)");
    if (m_valuation.research.medianCents)
        return QStringLiteral("middle half of sold (Product Research, %1 - the 90-day search counted no sale)").arg(periodText());
    return {};
}

QString ResultsController::medianLegend() const
{
    const std::optional<qint64> median = m_valuation.sold.medianCents ? m_valuation.sold.medianCents : m_valuation.research.medianCents;
    return median ? QStringLiteral("median %1").arg(moneyText(median)) : QString();
}

QString ResultsController::scaleLowText() const
{
    return m_bar.min < 0 ? QString() : moneyText(static_cast<qint64>(m_bar.min));
}

QString ResultsController::scaleHighText() const
{
    return m_bar.max < 0 ? QString() : moneyText(static_cast<qint64>(m_bar.max));
}

QString ResultsController::emptyMessage() const
{
    const lookup::LookupResult &result = m_saved.result;
    if (result.status != QLatin1String("complete") || result.queries.isEmpty() || !m_saved.rows.isEmpty())
        return {};
    QStringList quoted;
    for (const QString &query : result.queries)
        quoted.append(QLatin1Char('"') + query + QLatin1Char('"'));
    return QStringLiteral("eBay found nothing for %1 - check the brand and model (Wrong item? Correct it)")
        .arg(quoted.join(QStringLiteral(", ")));
}

QStringList ResultsController::lookupNotes() const
{
    QStringList lines;
    for (const QString &note : m_saved.result.notes) {
        // Shown in the identity bar instead.
        if (note != QLatin1String(lookup::kIdentityMismatch))
            lines.append(note);
    }
    return lines;
}

} // namespace app::ui
