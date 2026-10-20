#include "lookup/Records.h"

#include "lookup/AuctionTime.h"

#include <QJsonArray>

#include <limits>

namespace app::lookup {

namespace {

// A JSON number that is a whole number of cents (or a count) and not
// negative; std::nullopt for null, absent or anything else.
std::optional<qint64> wholeNumber(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (number < 0 || number > 9e15 || number != static_cast<double>(static_cast<qint64>(number)))
        return std::nullopt;
    return static_cast<qint64>(number);
}

std::optional<int> count(const QJsonValue &value)
{
    const std::optional<qint64> number = wholeNumber(value);
    if (!number || *number > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(*number);
}

std::optional<qint64> sum(std::optional<qint64> a, std::optional<qint64> b)
{
    if (!a || !b)
        return std::nullopt;
    return *a + *b;
}

} // namespace

std::optional<qint64> SearchRecord::totalCents() const
{
    return sum(priceCents, shippingCents);
}

SearchRecord SearchRecord::fromJson(const QJsonObject &item, bool sold)
{
    SearchRecord record;
    record.itemId = item.value(QStringLiteral("itemId")).toString();
    record.title = item.value(QStringLiteral("title")).toString();
    record.condition = item.value(QStringLiteral("condition")).toString();
    const QJsonObject price = item.value(QStringLiteral("price")).toObject();
    const std::optional<qint64> low = wholeNumber(price.value(QStringLiteral("minCents")));
    const std::optional<qint64> high = wholeNumber(price.value(QStringLiteral("maxCents")));
    if (low && high && *low == *high && price.value(QStringLiteral("currency")).toString() == QLatin1String("USD"))
        record.priceCents = low;
    record.priceText = item.value(QStringLiteral("priceText")).toString();
    record.shippingCents = wholeNumber(item.value(QStringLiteral("shippingCents")));
    record.format = item.value(QStringLiteral("format")).toString();
    record.bids = count(item.value(QStringLiteral("bids")));
    record.sellerName = item.value(QStringLiteral("seller")).toObject().value(QStringLiteral("name")).toString();
    record.bestOffer = item.value(QStringLiteral("bestOfferOffered")).toBool();
    record.timeLeft = item.value(QStringLiteral("timeLeft")).toString();
    record.auctionEndText = item.value(QStringLiteral("auctionEnd")).toString();
    if (!record.timeLeft.isEmpty()) {
        if (const std::optional<AuctionEnd> end = app::lookup::auctionEnd(record.timeLeft, record.auctionEndText, QDateTime::currentDateTime())) {
            record.auctionEnd = end->end;
            record.auctionEndSeconds = end->seconds;
        }
    }
    if (sold) {
        record.soldDate = item.value(QStringLiteral("soldDate")).toString();
        record.confirmed = item.value(QStringLiteral("priceStatus")).toString() == QLatin1String("shown");
    }
    return record;
}

ResearchRecord ResearchRecord::fromJson(const QJsonObject &row)
{
    ResearchRecord record;
    record.itemId = row.value(QStringLiteral("itemId")).toString();
    record.title = row.value(QStringLiteral("title")).toString();
    record.avgSoldPriceCents = wholeNumber(row.value(QStringLiteral("avgSoldPriceCents")));
    record.avgShippingCents = wholeNumber(row.value(QStringLiteral("avgShippingCents")));
    record.formatText = row.value(QStringLiteral("formatText")).toString();
    record.totalSold = count(row.value(QStringLiteral("totalSold")));
    record.lastSoldDate = row.value(QStringLiteral("lastSoldDate")).toString();
    return record;
}

std::optional<qint64> ItemRecord::totalCents() const
{
    return sum(priceCents, shippingCents);
}

QString ItemRecord::specific(const QString &label) const
{
    for (const Specific &entry : specifics) {
        if (entry.label.compare(label, Qt::CaseInsensitive) == 0)
            return entry.value;
    }
    return {};
}

ItemRecord ItemRecord::fromJson(const QJsonObject &data)
{
    ItemRecord record;
    record.itemId = data.value(QStringLiteral("itemId")).toString();
    record.title = data.value(QStringLiteral("title")).toString();
    record.currency = data.value(QStringLiteral("currency")).toString();
    if (record.currency == QLatin1String("USD"))
        record.priceCents = wholeNumber(data.value(QStringLiteral("priceCents")));
    record.shippingCents = wholeNumber(data.value(QStringLiteral("shippingCents")));
    // eBay's visible condition text, not the schema.org code.
    record.condition = data.value(QStringLiteral("conditionText")).toString();
    const QJsonObject seller = data.value(QStringLiteral("seller")).toObject();
    record.sellerName = seller.value(QStringLiteral("name")).toString();
    record.sellerFeedbackCount = count(seller.value(QStringLiteral("feedbackCount")));
    if (const QJsonValue percent = seller.value(QStringLiteral("positivePercent")); percent.isDouble()
        && percent.toDouble() >= 0 && percent.toDouble() <= 100)
        record.sellerPositivePercent = percent.toDouble();
    record.imageUrl = data.value(QStringLiteral("imageUrl")).toString();
    record.auction = data.value(QStringLiteral("auction")).toBool();
    record.bids = count(data.value(QStringLiteral("bids")));
    record.endsIn = data.value(QStringLiteral("endsIn")).toString();
    record.endsAt = data.value(QStringLiteral("endsAt")).toString();
    record.buyItNow = data.value(QStringLiteral("buyItNowOffered")).toBool();
    record.bestOffer = data.value(QStringLiteral("bestOfferOffered")).toBool();
    if (record.auction && !record.endsIn.isEmpty()) {
        if (const std::optional<AuctionEnd> end = app::lookup::auctionEnd(record.endsIn, record.endsAt, QDateTime::currentDateTime())) {
            record.auctionEnd = end->end;
            record.auctionEndSeconds = end->seconds;
        }
    }
    for (const QJsonValue &value : data.value(QStringLiteral("specifics")).toArray()) {
        const QJsonObject entry = value.toObject();
        record.specifics.append({entry.value(QStringLiteral("label")).toString(), entry.value(QStringLiteral("value")).toString()});
    }
    return record;
}

} // namespace app::lookup
