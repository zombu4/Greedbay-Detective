#include "lookup/LookupReport.h"

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

std::optional<qint64> researchTotal(const ResearchRecord &record)
{
    if (!record.avgSoldPriceCents || !record.avgShippingCents)
        return std::nullopt;
    return *record.avgSoldPriceCents + *record.avgShippingCents;
}

std::optional<qint64> summaryCents(const QJsonObject &summary, const char *key)
{
    const QJsonValue value = summary.value(QLatin1String(key));
    if (!value.isDouble())
        return std::nullopt;
    return static_cast<qint64>(value.toDouble());
}

void numbersLines(QStringList *out, const QList<ConditionNumbers> &levels)
{
    if (levels.isEmpty()) {
        out->append(QStringLiteral("  none"));
        return;
    }
    for (const ConditionNumbers &level : levels) {
        QString line = QStringLiteral("  %1: %2 sales").arg(conditionName(level.condition).leftJustified(18)).arg(level.included);
        if (level.medianCents)
            line += QStringLiteral(", median %1, P25-P75 %2-%3")
                        .arg(money(*level.medianCents), money(*level.p25Cents), money(*level.p75Cents));
        line += QStringLiteral(", confidence %1").arg(level.confidence);
        if (!level.outliers.isEmpty()) {
            QStringList values;
            for (const qint64 cents : level.outliers)
                values.append(money(cents));
            line += QStringLiteral(" | outliers left out: %1").arg(values.join(QStringLiteral(", ")));
        }
        if (level.unconfirmed > 0)
            line += QStringLiteral(" | unconfirmed (Best Offer etc.): %1, median %2")
                        .arg(level.unconfirmed)
                        .arg(moneyOr(level.unconfirmedMedianCents));
        out->append(line);
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

QList<Row> classifyRows(const LookupResult &result, const MatchWords &words)
{
    QList<Row> rows;
    auto add = [&](Row::Source source, qsizetype index, const QString &title, const QString &condition) {
        Row row;
        row.source = source;
        row.index = index;
        row.match = matchRow(title, condition, result.identity, words);
        row.included = row.match.bucket == Bucket::SameItem;
        rows.append(row);
    };
    for (qsizetype i = 0; i < result.sold.size(); ++i)
        add(Row::Source::Sold, i, result.sold.at(i).title, result.sold.at(i).condition);
    for (qsizetype i = 0; i < result.active.size(); ++i)
        add(Row::Source::Active, i, result.active.at(i).title, result.active.at(i).condition);
    for (qsizetype i = 0; i < result.research.size(); ++i)
        add(Row::Source::Research, i, result.research.at(i).title, QString());
    return rows;
}

Valuation valuate(const LookupResult &result, const QList<Row> &rows, const MatchWords &words,
                  const ValueSettings &settings)
{
    Valuation valuation;
    valuation.listing = conditionOf(result.item.title, result.item.condition, words);
    QList<Comp> sold;
    QList<Comp> research;
    for (const Row &row : rows) {
        if (!row.included)
            continue;
        if (row.source == Row::Source::Sold) {
            const SearchRecord &record = result.sold.at(row.index);
            sold.append({record.totalCents(), record.shippingCents, record.confirmed, row.match.condition});
        } else if (row.source == Row::Source::Research) {
            const ResearchRecord &record = result.research.at(row.index);
            research.append({researchTotal(record), record.avgShippingCents, true, row.match.condition});
        }
    }
    valuation.sold = valueByCondition(sold, settings);
    valuation.research = valueByCondition(research, settings);
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
    out.append(QStringLiteral("  price %1 + shipping %2 = total %3 | condition: %4")
                   .arg(moneyOr(item.priceCents), moneyOr(item.shippingCents), moneyOr(item.totalCents()),
                        conditionName(valuation.listing.condition)));
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
        const double times = static_cast<double>(*shipping.listingCents) / static_cast<double>(*shipping.compMedianCents);
        QString share;
        if (const std::optional<qint64> total = item.totalCents(); total && *total > 0)
            share = QStringLiteral(", %1% of the total")
                        .arg(QString::number(100.0 * static_cast<double>(*shipping.listingCents) / static_cast<double>(*total), 'f', 0));
        out.append(QStringLiteral("SHIPPING FLAG: %1 is %2 x the matched sales' median shipping %3 (+%4)%5")
                       .arg(money(*shipping.listingCents), QString::number(times, 'f', 1), money(*shipping.compMedianCents),
                            money(*shipping.listingCents - *shipping.compMedianCents), share));
        out.append(QString());
    }

    out.append(QStringLiteral("SOLD, MATCHED (sold search, totals = price + shipping, confirmed prices)"));
    numbersLines(&out, valuation.sold);
    out.append(QStringLiteral("PRODUCT RESEARCH, MATCHED (one value per listing: average sold + average shipping)"));
    numbersLines(&out, valuation.research);
    out.append(QStringLiteral("SHIPPING: listing %1 | median of %2 matched sales %3 | eBay Product Research average %4")
                   .arg(moneyOr(shipping.listingCents))
                   .arg(shipping.compsWithShipping)
                   .arg(moneyOr(shipping.compMedianCents), moneyOr(shipping.ebayAverageCents)));
    out.append(QStringLiteral("eBay's own Product Research summary (query 1, may mix models): average sold %1")
                   .arg(moneyOr(valuation.ebayAverageSoldCents)));
    if (!expectation.trimmed().isEmpty())
        out.append(QStringLiteral("your estimate: %1 (the numbers above are what the market paid)").arg(expectation.trimmed()));
    for (const QString &note : result.notes)
        out.append(QStringLiteral("note: %1").arg(note));
    out.append(QString());

    const Bucket order[] = {Bucket::SameItem, Bucket::Documentation, Bucket::PartFits, Bucket::Companion,
                            Bucket::Accessory, Bucket::Lot, Bucket::UnknownItem, Bucket::NoMatch};
    const struct {
        Row::Source source;
        const char *name;
    } sources[] = {{Row::Source::Sold, "SOLD"}, {Row::Source::Research, "PRODUCT RESEARCH"}, {Row::Source::Active, "FOR SALE NOW"}};
    for (const auto &source : sources) {
        for (const Bucket bucket : order) {
            QStringList lines;
            for (const Row &row : rows) {
                if (row.source != source.source || row.match.bucket != bucket)
                    continue;
                QString total;
                QString title;
                QString extra;
                if (row.source == Row::Source::Research) {
                    const ResearchRecord &record = result.research.at(row.index);
                    total = moneyOr(researchTotal(record));
                    title = record.title;
                    extra = QStringLiteral("%1 sold").arg(record.totalSold ? QString::number(*record.totalSold) : QStringLiteral("-"));
                } else {
                    const SearchRecord &record = row.source == Row::Source::Sold ? result.sold.at(row.index) : result.active.at(row.index);
                    total = moneyOr(record.totalCents());
                    title = record.title;
                    extra = row.source == Row::Source::Sold
                                ? (record.confirmed ? QStringLiteral("confirmed") : QStringLiteral("unconfirmed"))
                                : orDash(record.format);
                }
                lines.append(QStringLiteral("  [%1] %2 %3 %4 %5 | %6")
                                 .arg(row.included ? QStringLiteral("x") : QStringLiteral(" "), total.leftJustified(11),
                                      extra.leftJustified(11), conditionName(row.match.condition).leftJustified(18),
                                      cut(title), row.match.reasons.join(QStringLiteral("; "))));
            }
            if (lines.isEmpty())
                continue;
            out.append(QStringLiteral("%1 - %2 (%3)").arg(QString::fromLatin1(source.name), bucketName(bucket)).arg(lines.size()));
            out.append(lines);
        }
    }
    out.append(QString());
    out.append(QStringLiteral("pages loaded: %1 | time: %2 s | status: %3%4")
                   .arg(result.pagesLoaded)
                   .arg(QString::number(static_cast<double>(result.elapsedMs) / 1000.0, 'f', 1), result.status,
                        result.message.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(result.message)));
    return out.join(QLatin1Char('\n'));
}

} // namespace app::lookup
