#include "lookup/LookupReport.h"

#include <QJsonObject>
#include <QStringList>

#include <algorithm>

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

std::optional<qint64> summaryCents(const QJsonObject &summary, const char *key)
{
    const QJsonValue value = summary.value(QLatin1String(key));
    if (!value.isDouble())
        return std::nullopt;
    return static_cast<qint64>(value.toDouble());
}

QString numbersLine(const ValueNumbers &numbers)
{
    QString line = QStringLiteral("  %1 sales").arg(numbers.included);
    if (numbers.medianCents)
        line += QStringLiteral(", median %1, P25-P75 %2-%3")
                    .arg(money(*numbers.medianCents), money(*numbers.p25Cents), money(*numbers.p75Cents));
    line += QStringLiteral(", confidence %1").arg(numbers.confidence);
    if (!numbers.outliers.isEmpty()) {
        QStringList values;
        for (const qint64 cents : numbers.outliers)
            values.append(money(cents));
        line += QStringLiteral(" | outliers left out: %1").arg(values.join(QStringLiteral(", ")));
    }
    if (numbers.unconfirmed > 0)
        line += QStringLiteral(" | unconfirmed (Best Offer etc.): %1, median %2")
                    .arg(numbers.unconfirmed)
                    .arg(moneyOr(numbers.unconfirmedMedianCents));
    return line;
}

int bucketRank(Bucket bucket)
{
    const Bucket order[] = {Bucket::SameItem, Bucket::Documentation, Bucket::PartFits, Bucket::Companion, Bucket::Accessory,
                            Bucket::Lot, Bucket::UnknownItem, Bucket::ToCheck, Bucket::NoMatch};
    return static_cast<int>(std::find(std::begin(order), std::end(order), bucket) - std::begin(order));
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

std::optional<qint64> rowTotal(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).totalCents();
    case Row::Source::Active:
        return result.active.at(row.index).totalCents();
    case Row::Source::Research: {
        const ResearchRecord &record = result.research.at(row.index);
        if (!record.avgSoldPriceCents || !record.avgShippingCents)
            return std::nullopt;
        return *record.avgSoldPriceCents + *record.avgShippingCents;
    }
    }
    return std::nullopt;
}

QString rowCondition(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).condition;
    case Row::Source::Active:
        return result.active.at(row.index).condition;
    case Row::Source::Research:
        break;
    }
    return {};
}

QString rowTitle(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).title;
    case Row::Source::Active:
        return result.active.at(row.index).title;
    case Row::Source::Research:
        return result.research.at(row.index).title;
    }
    return {};
}

QString rowItemId(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).itemId;
    case Row::Source::Active:
        return result.active.at(row.index).itemId;
    case Row::Source::Research:
        return result.research.at(row.index).itemId;
    }
    return {};
}

QString rowDate(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).soldDate;
    case Row::Source::Research:
        return result.research.at(row.index).lastSoldDate;
    case Row::Source::Active:
        break;
    }
    return {};
}

QString rowSeller(const LookupResult &result, const Row &row)
{
    switch (row.source) {
    case Row::Source::Sold:
        return result.sold.at(row.index).sellerName;
    case Row::Source::Active:
        return result.active.at(row.index).sellerName;
    case Row::Source::Research:
        break;
    }
    return {};
}

RowShippingNote rowShippingNote(const LookupResult &result, const Row &row, const ShippingCheck &reference,
                                const ValueSettings &settings)
{
    if (row.source == Row::Source::Research)
        return {};
    const SearchRecord &record = row.source == Row::Source::Sold ? result.sold.at(row.index) : result.active.at(row.index);
    const std::optional<qint64> shipping = record.shippingCents;
    QStringList against;
    auto compare = [&](const QString &what, std::optional<qint64> referenceCents) {
        if (!shippingTooHigh(shipping, referenceCents, settings))
            return;
        against.append(*referenceCents > 0
                           ? QStringLiteral("%1x %2 (%3)")
                                 .arg(QString::number(static_cast<double>(*shipping) / static_cast<double>(*referenceCents), 'f', 1),
                                      what, money(*referenceCents))
                           : QStringLiteral("far above %1 (%2)").arg(what, money(*referenceCents)));
    };
    compare(QStringLiteral("the median shipping of the matched sales"), reference.compMedianCents);
    compare(QStringLiteral("eBay's average shipping for this item"), reference.ebayAverageCents);
    if (against.isEmpty())
        return {};
    RowShippingNote note;
    note.badge = QStringLiteral("SHIPPING %1").arg(money(*shipping));
    note.text = QStringLiteral("Shipping %1 is %2").arg(money(*shipping), against.join(QStringLiteral(" and ")));
    if (record.priceCents && *record.priceCents > 0)
        note.text += QStringLiteral("; the item itself is %1 (shipping %2x the price)")
                         .arg(money(*record.priceCents),
                              QString::number(static_cast<double>(*shipping) / static_cast<double>(*record.priceCents), 'f', 1));
    note.text += QStringLiteral(". A low price with high shipping is a common trap: check the total before buying.");
    return note;
}

QString rowSourceName(Row::Source source)
{
    switch (source) {
    case Row::Source::Sold:
        return QStringLiteral("sold");
    case Row::Source::Active:
        return QStringLiteral("active");
    case Row::Source::Research:
        break;
    }
    return QStringLiteral("research");
}

QList<qsizetype> rowOrder(const LookupResult &result, const QList<Row> &rows)
{
    QList<qsizetype> order;
    for (qsizetype i = 0; i < rows.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
        const int rankA = bucketRank(rows.at(a).match.bucket);
        const int rankB = bucketRank(rows.at(b).match.bucket);
        if (rankA != rankB)
            return rankA < rankB;
        const std::optional<qint64> totalA = rowTotal(result, rows.at(a));
        const std::optional<qint64> totalB = rowTotal(result, rows.at(b));
        if (totalA.has_value() != totalB.has_value())
            return totalA.has_value();
        return totalA && *totalA < *totalB;
    });
    return order;
}

QList<Row> classifyRows(const LookupResult &result, const MatchWords &words)
{
    QList<Row> rows;
    auto add = [&](Row::Source source, qsizetype index, const QString &title) {
        Row row;
        row.source = source;
        row.index = index;
        row.match = matchRow(title, result.identity, words);
        row.included = row.match.bucket == Bucket::SameItem;
        rows.append(row);
    };
    for (qsizetype i = 0; i < result.sold.size(); ++i)
        add(Row::Source::Sold, i, result.sold.at(i).title);
    for (qsizetype i = 0; i < result.active.size(); ++i)
        add(Row::Source::Active, i, result.active.at(i).title);
    for (qsizetype i = 0; i < result.research.size(); ++i)
        add(Row::Source::Research, i, result.research.at(i).title);
    return rows;
}

bool checkOwnListing(LookupResult *result, QList<Row> *rows, const MatchWords &words)
{
    const QString itemId = result->item.itemId;
    const QString source = result->identity.source;
    if (source == QLatin1String("user") || source == QLatin1String("saved")) {
        for (Row &row : *rows) {
            if (itemId.isEmpty() || rowItemId(*result, row) != itemId || row.match.bucket == Bucket::SameItem)
                continue;
            row.match.bucket = Bucket::SameItem;
            row.match.reasons.append(QStringLiteral("this listing: you set what it is"));
            row.included = true;
        }
        return true;
    }
    if (result->identity.model.isEmpty() || matchRow(result->item.title, result->identity, words).bucket != Bucket::NoMatch)
        return true;
    for (Row &row : *rows)
        row.included = false;
    const QString note = QString::fromLatin1(kIdentityMismatch);
    if (!result->notes.contains(note))
        result->notes.append(note);
    return false;
}

Valuation valuate(const LookupResult &result, const QList<Row> &rows, const ValueSettings &settings)
{
    Valuation valuation;
    QList<Comp> sold;
    QList<Comp> research;
    for (const Row &row : rows) {
        if (!row.included)
            continue;
        if (row.source == Row::Source::Sold) {
            const SearchRecord &record = result.sold.at(row.index);
            sold.append({record.totalCents(), record.shippingCents, record.confirmed});
        } else if (row.source == Row::Source::Research) {
            research.append({rowTotal(result, row), result.research.at(row.index).avgShippingCents, true});
        } else if (const SearchRecord &record = result.active.at(row.index); record.itemId != result.item.itemId) {
            const std::optional<qint64> total = rowTotal(result, row);
            if (record.format == QLatin1String("fixed_price") && total)
                valuation.forSale.append(*total);
            else if (record.format == QLatin1String("auction") && record.priceCents)
                valuation.auctions.append(*record.priceCents);
        }
    }
    std::sort(valuation.forSale.begin(), valuation.forSale.end());
    std::sort(valuation.auctions.begin(), valuation.auctions.end());
    if (const std::optional<qint64> listing = result.item.totalCents()) {
        valuation.cheaperForSale = static_cast<int>(
            std::count_if(valuation.forSale.cbegin(), valuation.forSale.cend(), [&](qint64 total) { return total < *listing; }));
    }
    valuation.sold = valueOf(sold, settings);
    valuation.research = valueOf(research, settings);
    valuation.shipping = checkShipping(result.item.shippingCents, sold,
                                       summaryCents(result.researchSummary, "avgShippingCents"), settings);
    valuation.ebayAverageSoldCents = summaryCents(result.researchSummary, "avgSoldPriceCents");
    return valuation;
}

QString reportText(const LookupResult &result, const QList<Row> &rows, const Valuation &valuation,
                   const QString &expectation)
{
    QStringList out;
    const ItemRecord &item = result.item;
    const Identity &identity = result.identity;
    out.append(QStringLiteral("LISTING %1: %2").arg(orDash(item.itemId), orDash(item.title)));
    out.append(QStringLiteral("  price %1 + shipping %2 = total %3 | eBay condition: %4")
                   .arg(moneyOr(item.priceCents), moneyOr(item.shippingCents), moneyOr(item.totalCents()), orDash(item.condition)));
    out.append(QStringLiteral("  identity: brand %1, model %2%3 (%4)")
                   .arg(orDash(identity.brandName), orDash(identity.model),
                        identity.productName.isEmpty() ? QString() : QStringLiteral(", \"%1\"").arg(identity.productName),
                        identity.confident ? identity.source : QStringLiteral("NOT CONFIDENT: %1").arg(identity.reason)));
    if (!identity.family.isEmpty())
        out.append(QStringLiteral("  same item also as: %1").arg(identity.family.join(QStringLiteral(", "))));
    for (const QString &note : identity.notes)
        out.append(QStringLiteral("  known issue: %1").arg(note));
    out.append(QStringLiteral("  queries: %1").arg(orDash(result.queries.join(QStringLiteral(" | ")))));
    out.append(QString());

    const ShippingCheck &shipping = valuation.shipping;
    if (shipping.flagged) {
        QString share;
        if (const std::optional<qint64> total = item.totalCents(); total && *total > 0)
            share = QStringLiteral(" (%1% of the total)")
                        .arg(QString::number(100.0 * static_cast<double>(*shipping.listingCents) / static_cast<double>(*total), 'f', 0));
        // "N x" only against a reference above $0.
        auto against = [&shipping](const char *what, qint64 reference) {
            const QString times = reference > 0
                                      ? QStringLiteral("%1 x ").arg(QString::number(static_cast<double>(*shipping.listingCents)
                                                                                        / static_cast<double>(reference), 'f', 1))
                                      : QString();
            return QStringLiteral("%1%2 %3 (+%4)").arg(times, QString::fromLatin1(what), money(reference),
                                                       money(*shipping.listingCents - reference));
        };
        QStringList parts;
        if (shipping.flaggedVsComps)
            parts.append(against("the matched sales' median shipping", *shipping.compMedianCents));
        if (shipping.flaggedVsEbay)
            parts.append(against("eBay's Product Research average shipping", *shipping.ebayAverageCents));
        out.append(QStringLiteral("SHIPPING FLAG: %1%2 is %3").arg(money(*shipping.listingCents), share,
                                                                   parts.join(QStringLiteral(" and "))));
        out.append(QString());
    }

    out.append(QStringLiteral("SOLD, SAME ITEM (sold search, totals = price + shipping, confirmed prices, every condition)"));
    out.append(numbersLine(valuation.sold));
    out.append(QStringLiteral("PRODUCT RESEARCH, SAME ITEM (one value per listing: average sold + average shipping)"));
    out.append(numbersLine(valuation.research));
    if (const std::optional<qint64> total = item.totalCents()) {
        QString line = QStringLiteral("THIS LISTING: total %1").arg(money(*total));
        if (valuation.sold.medianCents)
            line += QStringLiteral(" | sold median %1 (%2%3), P25-P75 %4-%5")
                        .arg(money(*valuation.sold.medianCents), *total >= *valuation.sold.medianCents ? QStringLiteral("+") : QString(),
                             money(*total - *valuation.sold.medianCents), money(*valuation.sold.p25Cents),
                             money(*valuation.sold.p75Cents));
        if (!valuation.forSale.isEmpty())
            line += QStringLiteral(" | for sale now: %1 of %2 same-item listings are cheaper, cheapest %3")
                        .arg(valuation.cheaperForSale)
                        .arg(valuation.forSale.size())
                        .arg(money(valuation.forSale.first()));
        out.append(line);
    }
    out.append(QStringLiteral("SHIPPING: listing %1 | median of %2 matched sales %3 | eBay Product Research average %4")
                   .arg(moneyOr(shipping.listingCents))
                   .arg(shipping.compsWithShipping)
                   .arg(moneyOr(shipping.compMedianCents), moneyOr(shipping.ebayAverageCents)));
    out.append(QStringLiteral("  (shipping costs shown by eBay can depend on the ship-to location eBay has for the app's "
                              "browser profile)"));
    out.append(QStringLiteral("eBay's own Product Research summary (query 1, may mix models): average sold %1")
                   .arg(moneyOr(valuation.ebayAverageSoldCents)));
    if (!expectation.trimmed().isEmpty())
        out.append(QStringLiteral("your estimate: %1 (the numbers above are what the market paid)").arg(expectation.trimmed()));
    for (const QString &note : result.notes)
        out.append(QStringLiteral("note: %1").arg(note));
    out.append(QString());

    const QList<qsizetype> order = rowOrder(result, rows);
    const struct {
        Row::Source source;
        const char *name;
    } sources[] = {{Row::Source::Active, "FOR SALE NOW"}, {Row::Source::Sold, "SOLD"}, {Row::Source::Research, "PRODUCT RESEARCH"}};
    for (const auto &source : sources) {
        Bucket current = Bucket::NoMatch;
        QStringList lines;
        int cheapest = 0;
        auto flush = [&]() {
            if (lines.isEmpty())
                return;
            out.append(QStringLiteral("%1 - %2 (%3)").arg(QString::fromLatin1(source.name), bucketName(current)).arg(lines.size()));
            out.append(lines);
            lines.clear();
        };
        for (const qsizetype i : order) {
            const Row &row = rows.at(i);
            if (row.source != source.source)
                continue;
            if (row.match.bucket != current)
                flush();
            current = row.match.bucket;
            QString mark;
            const bool ownListing = row.source == Row::Source::Active && result.active.at(row.index).itemId == item.itemId;
            if (ownListing)
                mark = QStringLiteral("THIS LISTING ");
            else if (row.source == Row::Source::Active && row.included && rowTotal(result, row) && cheapest < kCheapestMarked)
                mark = QStringLiteral("CHEAPEST %1 ").arg(++cheapest);
            QString extra;
            if (row.source == Row::Source::Research) {
                const ResearchRecord &record = result.research.at(row.index);
                extra = QStringLiteral("%1 sold").arg(record.totalSold ? QString::number(*record.totalSold) : QStringLiteral("-"));
            } else if (row.source == Row::Source::Sold) {
                extra = result.sold.at(row.index).confirmed ? QStringLiteral("confirmed") : QStringLiteral("unconfirmed");
            } else {
                extra = orDash(result.active.at(row.index).format);
            }
            lines.append(QStringLiteral("  [%1] %2 %3 %4 %5%6 | %7")
                             .arg(row.included ? QStringLiteral("x") : QStringLiteral(" "),
                                  moneyOr(rowTotal(result, row)).leftJustified(11), extra.leftJustified(11),
                                  orDash(rowCondition(result, row)).leftJustified(24), mark, cut(rowTitle(result, row)),
                                  row.match.reasons.join(QStringLiteral("; "))));
        }
        flush();
    }
    out.append(QString());
    out.append(QStringLiteral("pages loaded: %1 | time: %2 s | status: %3%4")
                   .arg(result.pagesLoaded)
                   .arg(QString::number(static_cast<double>(result.elapsedMs) / 1000.0, 'f', 1), result.status,
                        result.message.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(result.message)));
    return out.join(QLatin1Char('\n'));
}

} // namespace app::lookup
