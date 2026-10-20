#include "lookup/LookupExport.h"

#include "lookup/LookupReport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace app::lookup {

namespace {

QString csvField(QString text)
{
    // A seller's title starting with = + - or @ would be a formula in a
    // spreadsheet: a leading apostrophe keeps it text.
    if (!text.isEmpty() && QStringLiteral("=+-@").contains(text.front()))
        text.prepend(QLatin1Char('\''));
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n'))
        && !text.contains(QLatin1Char('\r')))
        return text;
    QString quoted = text;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString centsText(const std::optional<qint64> &cents)
{
    return cents ? QString::number(*cents) : QString();
}

QJsonValue centsJson(const std::optional<qint64> &cents)
{
    return cents ? QJsonValue(static_cast<double>(*cents)) : QJsonValue(QJsonValue::Null);
}

} // namespace

QByteArray lookupsCsv(const QList<LookupSummary> &lookups)
{
    QStringList lines{QStringLiteral("id,checked_utc,input,item_id,title,brand,model,status,listing_total_cents,sold_count,"
                                     "sold_median_cents,sold_confidence,shipping_flagged,for_sale_count,cheaper_for_sale,"
                                     "cheapest_for_sale_cents")};
    for (const LookupSummary &lookup : lookups) {
        const QStringList fields{
            QString::number(lookup.id),
            lookup.createdAt.toUTC().toString(Qt::ISODate),
            lookup.input,
            lookup.itemId,
            lookup.title,
            lookup.brand,
            lookup.model,
            lookup.status,
            centsText(lookup.listingTotalCents),
            QString::number(lookup.soldCount),
            centsText(lookup.soldMedianCents),
            lookup.soldConfidence,
            lookup.shippingFlagged ? QStringLiteral("yes") : QStringLiteral("no"),
            QString::number(lookup.forSaleCount),
            QString::number(lookup.cheaperForSale),
            centsText(lookup.cheapestForSaleCents),
        };
        QStringList quoted;
        for (const QString &field : fields)
            quoted.append(csvField(field));
        lines.append(quoted.join(QLatin1Char(',')));
    }
    return (lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}

QJsonObject lookupJson(const SavedLookup &lookup)
{
    const LookupResult &result = lookup.result;
    const ItemRecord &item = result.item;
    QJsonArray specifics;
    for (const ItemRecord::Specific &specific : item.specifics)
        specifics.append(QJsonObject{{QStringLiteral("label"), specific.label}, {QStringLiteral("value"), specific.value}});
    QJsonArray rows;
    for (const Row &row : lookup.rows) {
        rows.append(QJsonObject{
            {QStringLiteral("source"), rowSourceName(row.source)},
            {QStringLiteral("itemId"), rowItemId(result, row)},
            {QStringLiteral("title"), rowTitle(result, row)},
            {QStringLiteral("totalCents"), centsJson(rowTotal(result, row))},
            {QStringLiteral("condition"), rowCondition(result, row)},
            {QStringLiteral("date"), rowDate(result, row)},
            {QStringLiteral("bucket"), LookupStore::bucketId(row.match.bucket)},
            {QStringLiteral("reasons"), QJsonArray::fromStringList(row.match.reasons)},
            {QStringLiteral("counted"), row.included},
        });
    }
    return QJsonObject{
        {QStringLiteral("id"), static_cast<double>(lookup.id)},
        {QStringLiteral("checkedUtc"), lookup.createdAt.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("input"), lookup.input},
        {QStringLiteral("status"), result.status},
        {QStringLiteral("listing"),
         QJsonObject{{QStringLiteral("itemId"), item.itemId},
                     {QStringLiteral("title"), item.title},
                     {QStringLiteral("priceCents"), centsJson(item.priceCents)},
                     {QStringLiteral("shippingCents"), centsJson(item.shippingCents)},
                     {QStringLiteral("totalCents"), centsJson(item.totalCents())},
                     {QStringLiteral("currency"), item.currency},
                     {QStringLiteral("condition"), item.condition},
                     {QStringLiteral("sellerName"), item.sellerName},
                     {QStringLiteral("specifics"), specifics}}},
        {QStringLiteral("identity"),
         QJsonObject{{QStringLiteral("brand"), result.identity.brandName},
                     {QStringLiteral("model"), result.identity.model},
                     {QStringLiteral("productName"), result.identity.productName},
                     {QStringLiteral("source"), result.identity.source}}},
        {QStringLiteral("searches"), QJsonArray::fromStringList(result.queries)},
        {QStringLiteral("rows"), rows},
    };
}

QByteArray lookupsJson(const QList<SavedLookup> &lookups)
{
    QJsonArray list;
    for (const SavedLookup &lookup : lookups)
        list.append(lookupJson(lookup));
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("lookups"), list}}).toJson(QJsonDocument::Indented);
}

} // namespace app::lookup
