#include "lookup/LookupReport.h"

#include "lookup/Stats.h"

#include <QJsonObject>
#include <QStringList>

namespace app::lookup {

namespace {

constexpr int kTitleWidth = 60;

QString cut(const QString &text)
{
    return text.size() <= kTitleWidth ? text : text.left(kTitleWidth - 3) + QStringLiteral("...");
}

QString moneyOr(const std::optional<qint64> &cents)
{
    return cents ? money(*cents) : QStringLiteral("-");
}

QString orDash(const QString &text)
{
    return text.isEmpty() ? QStringLiteral("-") : text;
}

QString row(const QStringList &cells, const QList<int> &widths)
{
    QString line = QStringLiteral("  ");
    for (qsizetype i = 0; i < cells.size(); ++i) {
        const bool last = i + 1 == cells.size();
        line += last ? cells.at(i) : cells.at(i).leftJustified(widths.value(i), QLatin1Char(' ')) + QLatin1Char(' ');
    }
    return line;
}

void soldTable(QStringList *out, const QString &heading, const QList<SearchRecord> &records)
{
    const QList<int> widths{11, 11, 9, 11, 13};
    out->append(QStringLiteral("%1 (%2)").arg(heading).arg(records.size()));
    if (records.isEmpty())
        return;
    out->append(row({QStringLiteral("total"), QStringLiteral("price"), QStringLiteral("shipping"), QStringLiteral("sold"),
                     QStringLiteral("item"), QStringLiteral("title")}, widths));
    for (const SearchRecord &record : records) {
        out->append(row({moneyOr(record.totalCents()), record.priceCents ? money(*record.priceCents) : orDash(record.priceText),
                         moneyOr(record.shippingCents), orDash(record.soldDate), orDash(record.itemId), cut(record.title)},
                        widths));
    }
}

} // namespace

QString money(qint64 cents)
{
    const qint64 magnitude = cents < 0 ? -cents : cents;
    QString whole = QString::number(magnitude / 100);
    for (qsizetype i = whole.size() - 3; i > 0; i -= 3)
        whole.insert(i, QLatin1Char(','));
    const QString text = QStringLiteral("$%1.%2").arg(whole).arg(magnitude % 100, 2, 10, QLatin1Char('0'));
    return cents < 0 ? QLatin1Char('-') + text : text;
}

LookupNumbers computeNumbers(const LookupResult &result)
{
    LookupNumbers numbers;
    QList<qint64> totals;
    QList<qint64> shipping;
    for (const SearchRecord &record : result.sold) {
        if (!record.confirmed)
            continue;
        if (const std::optional<qint64> total = record.totalCents())
            totals.append(*total);
        if (record.shippingCents)
            shipping.append(*record.shippingCents);
    }
    numbers.confirmedWithTotal = static_cast<int>(totals.size());
    numbers.medianCents = percentile(totals, 50);
    numbers.p25Cents = percentile(totals, 25);
    numbers.p75Cents = percentile(totals, 75);
    numbers.confirmedWithShipping = static_cast<int>(shipping.size());
    numbers.medianShippingCents = percentile(shipping, 50);
    return numbers;
}

QString reportText(const LookupResult &result)
{
    QStringList out;
    const ItemRecord &item = result.item;
    out.append(QStringLiteral("LISTING %1: %2").arg(orDash(item.itemId), orDash(item.title)));
    out.append(QStringLiteral("  price %1 + shipping %2 = total %3 | condition %4 | seller %5")
                   .arg(moneyOr(item.priceCents), moneyOr(item.shippingCents), moneyOr(item.totalCents()),
                        orDash(item.condition), orDash(item.sellerName)));
    out.append(QStringLiteral("  brand: %1 | model tokens: %2")
                   .arg(orDash(result.identity.brand), orDash(result.identity.modelTokens.join(QStringLiteral(", ")))));
    QStringList queries;
    for (qsizetype i = 0; i < result.queries.size(); ++i)
        queries.append(QStringLiteral("%1 \"%2\"").arg(i + 1).arg(result.queries.at(i)));
    out.append(QStringLiteral("  queries: %1").arg(orDash(queries.join(QStringLiteral(", ")))));
    out.append(QString());

    QList<SearchRecord> confirmed;
    QList<SearchRecord> unconfirmed;
    for (const SearchRecord &record : result.sold)
        (record.confirmed ? confirmed : unconfirmed).append(record);
    soldTable(&out, QStringLiteral("SOLD, CONFIRMED (price shown is the price paid)"), confirmed);
    out.append(QString());
    soldTable(&out, QStringLiteral("SOLD, UNCONFIRMED (the price paid is not shown: Best offer accepted, an offer row the "
                                   "app does not know, or no Product Research access)"), unconfirmed);
    out.append(QString());

    const QList<int> activeWidths{11, 21, 9, 12, 13};
    out.append(QStringLiteral("FOR SALE NOW (%1)").arg(result.active.size()));
    if (!result.active.isEmpty()) {
        out.append(row({QStringLiteral("total"), QStringLiteral("price"), QStringLiteral("shipping"), QStringLiteral("format"),
                        QStringLiteral("item"), QStringLiteral("title")}, activeWidths));
        for (const SearchRecord &record : result.active) {
            QString format = orDash(record.format);
            if (record.bids)
                format = QStringLiteral("%1 bids").arg(*record.bids);
            out.append(row({moneyOr(record.totalCents()), record.priceCents ? money(*record.priceCents) : orDash(record.priceText),
                            moneyOr(record.shippingCents), format, orDash(record.itemId), cut(record.title)}, activeWidths));
        }
    }
    out.append(QString());

    const QList<int> researchWidths{11, 11, 6, 11, 13};
    out.append(QStringLiteral("PRODUCT RESEARCH ROWS (%1; one row = one listing, average of its sales)").arg(result.research.size()));
    if (result.researchUnavailable)
        out.append(QStringLiteral("  no Product Research access on this account"));
    if (!result.research.isEmpty()) {
        out.append(row({QStringLiteral("avg price"), QStringLiteral("avg ship"), QStringLiteral("sold"), QStringLiteral("last sold"),
                        QStringLiteral("item"), QStringLiteral("title")}, researchWidths));
        for (const ResearchRecord &record : result.research) {
            out.append(row({moneyOr(record.avgSoldPriceCents), moneyOr(record.avgShippingCents),
                            record.totalSold ? QString::number(*record.totalSold) : QStringLiteral("-"), orDash(record.lastSoldDate),
                            orDash(record.itemId), cut(record.title)}, researchWidths));
        }
    }
    out.append(QString());

    const LookupNumbers numbers = computeNumbers(result);
    out.append(QStringLiteral("NUMBERS (confirmed sold-search records only, every one found: no matching yet; Product "
                              "Research rows are not in these numbers)"));
    out.append(QStringLiteral("  confirmed sold totals: %1").arg(numbers.confirmedWithTotal));
    if (numbers.medianCents) {
        out.append(QStringLiteral("  median %1, P25 %2, P75 %3")
                       .arg(money(*numbers.medianCents), money(*numbers.p25Cents), money(*numbers.p75Cents)));
        if (const std::optional<qint64> total = item.totalCents()) {
            QString position;
            if (*total < *numbers.p25Cents)
                position = QStringLiteral("%1 below P25").arg(money(*numbers.p25Cents - *total));
            else if (*total > *numbers.p75Cents)
                position = QStringLiteral("%1 above P75").arg(money(*total - *numbers.p75Cents));
            else
                position = QStringLiteral("between P25 and P75");
            out.append(QStringLiteral("  listing total %1: %2; %3 against the median")
                           .arg(money(*total), position,
                                (*total >= *numbers.medianCents ? QStringLiteral("+") : QString())
                                    + money(*total - *numbers.medianCents)));
        } else {
            out.append(QStringLiteral("  listing total unknown (price or shipping not read)"));
        }
    } else {
        out.append(QStringLiteral("  no confirmed sold total: no median"));
    }
    if (numbers.medianShippingCents && item.shippingCents) {
        out.append(QStringLiteral("  shipping: listing %1, median of %2 confirmed sold %3 (%4%5)")
                       .arg(money(*item.shippingCents))
                       .arg(numbers.confirmedWithShipping)
                       .arg(money(*numbers.medianShippingCents),
                            *item.shippingCents >= *numbers.medianShippingCents ? QStringLiteral("+") : QString(),
                            money(*item.shippingCents - *numbers.medianShippingCents)));
    } else {
        out.append(QStringLiteral("  shipping: not comparable (listing shipping %1, confirmed sold with shipping %2)")
                       .arg(moneyOr(item.shippingCents))
                       .arg(numbers.confirmedWithShipping));
    }
    if (!result.researchSummary.isEmpty()) {
        const QJsonObject summary = result.researchSummary;
        auto cents = [&summary](const char *key) {
            const QJsonValue value = summary.value(QLatin1String(key));
            return value.isDouble() ? money(static_cast<qint64>(value.toDouble())) : QStringLiteral("-");
        };
        out.append(QStringLiteral("  Product Research summary (eBay's, for query 1): avg sold price %1, avg shipping %2, "
                                  "total sellers %3")
                       .arg(cents("avgSoldPriceCents"), cents("avgShippingCents"),
                            summary.value(QStringLiteral("totalSellers")).isDouble()
                                ? QString::number(summary.value(QStringLiteral("totalSellers")).toInteger())
                                : QStringLiteral("-")));
    }
    for (const QString &note : result.notes)
        out.append(QStringLiteral("  note: %1").arg(note));
    out.append(QString());
    out.append(QStringLiteral("pages loaded: %1 | time: %2 s | status: %3%4")
                   .arg(result.pagesLoaded)
                   .arg(QString::number(static_cast<double>(result.elapsedMs) / 1000.0, 'f', 1), result.status,
                        result.message.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(result.message)));
    return out.join(QLatin1Char('\n'));
}

} // namespace app::lookup
