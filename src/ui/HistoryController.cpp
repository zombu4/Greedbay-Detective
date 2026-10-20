#include "ui/HistoryController.h"

#include "lookup/Matcher.h"
#include "ui/Format.h"

#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>

namespace app::ui {

namespace {

// Every flag of one check, each {text, kind} as the Pill shows it.
QVariantList flagsOf(const lookup::LookupSummary &check, const lookup::MatchWords *words)
{
    QVariantList flags;
    auto add = [&flags](const QString &text, const QString &kind) {
        flags.append(QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("kind"), kind}});
    };
    if (check.status == QLatin1String("partial"))
        add(QStringLiteral("Cancelled before the end"), QStringLiteral("warn"));
    else if (check.status == QLatin1String("failed"))
        add(QStringLiteral("Stopped before the end"), QStringLiteral("warn"));
    if (words && !lookup::likelyBrokenReason(check.title, check.conditionText, *words).isEmpty())
        add(QStringLiteral("Likely broken"), QStringLiteral("danger"));
    if (check.shippingFlagged) {
        add(check.shippingFlagRatio ? QStringLiteral("Shipping %1x normal").arg(QString::number(*check.shippingFlagRatio, 'f', 1))
                                    : QStringLiteral("Shipping above normal"),
            QStringLiteral("warn"));
    }
    if (check.cheaperForSale > 0)
        add(QStringLiteral("%1 cheaper for sale").arg(check.cheaperForSale), QStringLiteral("info"));
    return flags;
}

double msOf(const QDateTime &time)
{
    return static_cast<double>(time.toMSecsSinceEpoch());
}

// A sale's place on the time axis: noon UTC of its day.
QDateTime noonOf(const QDate &date)
{
    return QDateTime(date, QTime(12, 0), QTimeZone::UTC);
}

} // namespace

HistoryController::HistoryController(const lookup::LookupStore &store, const lookup::KnownModels *known,
                                     const lookup::MatchWords *words, QObject *parent)
    : QObject(parent), m_store(store), m_known(known), m_words(words)
{
}

void HistoryController::refresh()
{
    m_all.clear();
    m_message.clear();
    if (!m_known) {
        m_message = QStringLiteral("The app's matching data cannot be read, so no history can be shown.");
    } else {
        QString error;
        const std::optional<QList<lookup::LookupSummary>> lookups = m_store.recent(0, &error);
        if (!lookups)
            m_message = QStringLiteral("The saved lookups cannot be read: %1").arg(error);
        else
            m_all = lookup::historyItems(*lookups, *m_known);
    }
    m_items.clear();
    for (const lookup::HistoryItem &item : std::as_const(m_all))
        m_items.append(QVariantMap{{QStringLiteral("key"), item.key}, {QStringLiteral("name"), item.name}});
    if (std::none_of(m_all.cbegin(), m_all.cend(), [this](const lookup::HistoryItem &item) { return item.key == m_currentKey; }))
        m_currentKey = m_all.isEmpty() ? QString() : m_all.first().key;
    if (m_message.isEmpty() && m_all.isEmpty())
        m_message = QStringLiteral("No checked items yet. Check a listing on the Lookups screen; what the same item sold for shows "
                                   "here, and every later check adds to it.");
    rebuild();
}

void HistoryController::select(const QString &key)
{
    if (key == m_currentKey
        || std::none_of(m_all.cbegin(), m_all.cend(), [&key](const lookup::HistoryItem &item) { return item.key == key; }))
        return;
    m_currentKey = key;
    rebuild();
}

void HistoryController::setWindow(int days)
{
    if (days == m_windowDays || std::find(std::begin(kWindows), std::end(kWindows), days) == std::end(kWindows))
        return;
    m_windowDays = days;
    rebuild();
}

void HistoryController::recheck()
{
    if (!m_recheckInput.isEmpty())
        emit recheckRequested(m_recheckInput);
}

void HistoryController::rebuild()
{
    m_heading.clear();
    m_summary.clear();
    m_sales.clear();
    m_checks.clear();
    m_bandLow = m_bandHigh = m_median = -1;
    m_medianLabel.clear();
    m_recheckInput.clear();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDate today = now.date();
    const QDate first = today.addDays(-(m_windowDays - 1));
    m_minTime = msOf(QDateTime(first, QTime(0, 0), QTimeZone::UTC));
    m_maxTime = msOf(now);
    m_startLabel = dateText(first);
    m_endLabel = dateText(today);
    m_minCents = 0;
    m_maxCents = 0;
    m_lowLabel.clear();
    m_highLabel.clear();

    const auto item = std::find_if(m_all.cbegin(), m_all.cend(),
                                   [this](const lookup::HistoryItem &entry) { return entry.key == m_currentKey; });
    if (item == m_all.cend()) {
        emit changed();
        return;
    }
    m_heading = QStringLiteral("%1 - sold prices over time").arg(item->name);
    m_recheckInput = item->checks.first().input;

    QList<qint64> lookupIds;
    for (const lookup::LookupSummary &check : item->checks)
        lookupIds.append(check.id);
    QString error;
    const std::optional<QList<lookup::HistorySaleRow>> rows = m_store.historySales(lookupIds, &error);
    if (!rows) {
        m_summary = QStringLiteral("The sales of this item cannot be read: %1").arg(error);
        emit changed();
        return;
    }
    const lookup::HistoryPoints points = lookup::historyPoints(*rows, today, m_windowDays);

    QList<qint64> plotted;
    for (const lookup::HistorySale &sale : points.sales) {
        m_sales.append(QVariantMap{
            {QStringLiteral("time"), msOf(noonOf(sale.soldOn))},
            {QStringLiteral("cents"), static_cast<double>(sale.totalCents)},
            {QStringLiteral("label"), QStringLiteral("%1 sold %2").arg(moneyText(sale.totalCents), dateText(sale.soldOn))},
        });
        plotted.append(sale.totalCents);
    }
    for (const lookup::LookupSummary &check : item->checks) {
        const bool inWindow = check.createdAt.isValid() && check.createdAt.toUTC().date() >= first;
        const double cents = check.listingTotalCents && inWindow ? static_cast<double>(*check.listingTotalCents) : -1;
        if (cents >= 0)
            plotted.append(*check.listingTotalCents);
        m_checks.append(QVariantMap{
            {QStringLiteral("lookupId"), check.id},
            {QStringLiteral("time"), msOf(check.createdAt)},
            {QStringLiteral("cents"), cents},
            {QStringLiteral("checked"), whenText(check.createdAt, QDateTime::currentDateTime())},
            {QStringLiteral("itemId"), check.itemId},
            {QStringLiteral("total"), moneyText(check.listingTotalCents)},
            {QStringLiteral("medianThen"), moneyText(check.soldMedianCents)},
            {QStringLiteral("flags"), flagsOf(check, m_words)},
        });
    }

    if (!plotted.isEmpty()) {
        const auto [low, high] = std::minmax_element(plotted.cbegin(), plotted.cend());
        // A tenth of the range (at least $1) above and below, so no dot sits
        // on the edge.
        const qint64 pad = std::max<qint64>(100, (*high - *low) / 10);
        m_minCents = static_cast<double>(std::max<qint64>(0, *low - pad));
        m_maxCents = static_cast<double>(*high + pad);
        m_lowLabel = moneyText(static_cast<qint64>(m_minCents));
        m_highLabel = moneyText(static_cast<qint64>(m_maxCents));
    }
    if (points.p25Cents && points.p75Cents) {
        m_bandLow = static_cast<double>(*points.p25Cents);
        m_bandHigh = static_cast<double>(*points.p75Cents);
    }
    if (points.medianCents) {
        m_median = static_cast<double>(*points.medianCents);
        m_medianLabel = QStringLiteral("median %1").arg(moneyText(points.medianCents));
    }
    const QString window = m_windowDays == 90 ? QStringLiteral("90 days") : m_windowDays == 365 ? QStringLiteral("year")
                                                                                                : QStringLiteral("3 years");
    if (points.sales.isEmpty()) {
        m_summary = QStringLiteral("No counted sales of this item in the last %1. Re-check it, or pick a longer window.").arg(window);
    } else {
        m_summary = QStringLiteral("%1 in the last %2, totals incl. shipping: median %3, middle half %4 - %5")
                        .arg(countText(points.sales.size(), QStringLiteral("sale"), QStringLiteral("sales")), window,
                             moneyText(points.medianCents), moneyText(points.p25Cents), moneyText(points.p75Cents));
    }
    emit changed();
}

} // namespace app::ui
