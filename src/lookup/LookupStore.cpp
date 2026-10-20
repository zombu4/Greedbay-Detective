#include "lookup/LookupStore.h"

#include "core/Version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace app::lookup {

namespace {

const struct {
    Bucket bucket;
    const char *id;
} kBucketIds[] = {
    {Bucket::SameItem, "same_item"},       {Bucket::Documentation, "documentation"}, {Bucket::PartFits, "part_fits"},
    {Bucket::Companion, "companion"},      {Bucket::Accessory, "accessory"},         {Bucket::Lot, "lot"},
    {Bucket::UnknownItem, "unknown_item"}, {Bucket::ToCheck, "to_check"},           {Bucket::NoMatch, "not_this_item"},
};

QVariant optionalValue(const std::optional<qint64> &value)
{
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<qint64>());
}

QVariant optionalInt(const std::optional<int> &value)
{
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<int>());
}

// Empty text is stored as NULL.
QVariant textOrNull(const QString &text)
{
    return text.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(text);
}

// Text for a NOT NULL column: a null QString would be bound as NULL.
QString notNull(const QString &text)
{
    return text.isNull() ? QStringLiteral("") : text;
}

QVariant timeOrNull(const std::optional<QDateTime> &time)
{
    return time ? QVariant(time->toUTC().toString(Qt::ISODateWithMs)) : QVariant(QMetaType::fromType<QString>());
}

std::optional<QDateTime> optionalTime(const QVariant &value)
{
    if (value.isNull())
        return std::nullopt;
    const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    return time.isValid() ? std::optional<QDateTime>(time) : std::nullopt;
}

std::optional<qint64> optionalCents(const QVariant &value)
{
    if (value.isNull())
        return std::nullopt;
    return value.toLongLong();
}

std::optional<int> optionalCount(const QVariant &value)
{
    if (value.isNull())
        return std::nullopt;
    return value.toInt();
}

QString jsonList(const QStringList &list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
}

QStringList listFromJson(const QVariant &value)
{
    QStringList list;
    for (const QJsonValue &entry : QJsonDocument::fromJson(value.toString().toUtf8()).array())
        list.append(entry.toString());
    return list;
}

QString runnerStatusToStored(const QString &status)
{
    if (status == QLatin1String("complete"))
        return QStringLiteral("complete");
    if (status == QLatin1String("cancelled"))
        return QStringLiteral("partial");
    return QStringLiteral("failed");
}

QString storedStatusToRunner(const QString &status)
{
    if (status == QLatin1String("complete"))
        return QStringLiteral("complete");
    if (status == QLatin1String("partial"))
        return QStringLiteral("cancelled");
    return QStringLiteral("stopped");
}

bool run(QSqlQuery &query, QString *error)
{
    if (query.exec())
        return true;
    *error = query.lastError().text();
    return false;
}

// The numbers the recent-lookups card shows, bound by name.
void bindNumbers(QSqlQuery &query, const LookupResult &result, const Valuation &valuation)
{
    query.bindValue(QStringLiteral(":listing_total_cents"), optionalValue(result.item.totalCents()));
    query.bindValue(QStringLiteral(":sold_count"), valuation.sold.included);
    query.bindValue(QStringLiteral(":sold_median_cents"), optionalValue(valuation.sold.medianCents));
    query.bindValue(QStringLiteral(":sold_p25_cents"), optionalValue(valuation.sold.p25Cents));
    query.bindValue(QStringLiteral(":sold_p75_cents"), optionalValue(valuation.sold.p75Cents));
    query.bindValue(QStringLiteral(":sold_confidence"), valuation.sold.confidence);
    const ShippingCheck &shipping = valuation.shipping;
    // The ratio against eBay's average when that flagged it, else against
    // the matched sales' median.
    const std::optional<qint64> reference = shipping.flaggedVsEbay ? shipping.ebayAverageCents : shipping.compMedianCents;
    QVariant ratio(QMetaType::fromType<double>());
    if (shipping.flagged && reference && *reference > 0)
        ratio = static_cast<double>(*shipping.listingCents) / static_cast<double>(*reference);
    query.bindValue(QStringLiteral(":shipping_flagged"), shipping.flagged ? 1 : 0);
    query.bindValue(QStringLiteral(":shipping_flag_ratio"), ratio);
    query.bindValue(QStringLiteral(":for_sale_count"), static_cast<int>(valuation.forSale.size()));
    query.bindValue(QStringLiteral(":cheaper_for_sale"), valuation.cheaperForSale);
    query.bindValue(QStringLiteral(":cheapest_for_sale_cents"),
                    valuation.forSale.isEmpty() ? QVariant(QMetaType::fromType<qint64>()) : QVariant(valuation.forSale.first()));
}

const char kNumberAssignments[] =
    "listing_total_cents = :listing_total_cents, sold_count = :sold_count, sold_median_cents = :sold_median_cents, "
    "sold_p25_cents = :sold_p25_cents, sold_p75_cents = :sold_p75_cents, sold_confidence = :sold_confidence, "
    "shipping_flagged = :shipping_flagged, shipping_flag_ratio = :shipping_flag_ratio, for_sale_count = :for_sale_count, "
    "cheaper_for_sale = :cheaper_for_sale, cheapest_for_sale_cents = :cheapest_for_sale_cents";

QString rowSourceId(Row::Source source)
{
    switch (source) {
    case Row::Source::Sold:
        return QStringLiteral("sold");
    case Row::Source::Active:
        return QStringLiteral("active");
    case Row::Source::Research:
        return QStringLiteral("research");
    }
    return {};
}

} // namespace

LookupStore::LookupStore(QString connectionName) : m_connectionName(std::move(connectionName)) {}

QString LookupStore::bucketId(Bucket bucket)
{
    for (const auto &entry : kBucketIds) {
        if (entry.bucket == bucket)
            return QString::fromLatin1(entry.id);
    }
    return {};
}

std::optional<Bucket> LookupStore::bucketFromId(const QString &id)
{
    for (const auto &entry : kBucketIds) {
        if (id == QLatin1String(entry.id))
            return entry.bucket;
    }
    return std::nullopt;
}

std::optional<qint64> LookupStore::save(const QString &input, const LookupResult &result, const QList<Row> &rows,
                                        const Valuation &valuation, const Depths &depths, int historyDays,
                                        QString *error) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        *error = db.lastError().text();
        return std::nullopt;
    }
    auto fail = [&]() -> std::optional<qint64> {
        db.rollback();
        return std::nullopt;
    };

    const ItemRecord &item = result.item;
    const Identity &identity = result.identity;
    QJsonArray specifics;
    for (const ItemRecord::Specific &specific : item.specifics)
        specifics.append(QJsonObject{{QStringLiteral("label"), specific.label}, {QStringLiteral("value"), specific.value}});

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO lookups (created_at, input, item_id, marketplace, status, message, app_version, title, price_cents, "
        "shipping_cents, currency, condition_text, seller_name, image_url, specifics, brand, model, product_name, "
        "identity_source, identity_confident, identity_reason, family, known_issues, queries, sold_depth, active_depth, "
        "research_depth, history_days, research_unavailable, research_summary, notes, pages_loaded, elapsed_ms, "
        "listing_total_cents, sold_count, sold_median_cents, sold_p25_cents, sold_p75_cents, sold_confidence, "
        "shipping_flagged, shipping_flag_ratio, for_sale_count, cheaper_for_sale, cheapest_for_sale_cents) VALUES "
        "(:created_at, :input, :item_id, 'EBAY_US', :status, :message, :app_version, :title, :price_cents, "
        ":shipping_cents, :currency, :condition_text, :seller_name, :image_url, :specifics, :brand, :model, "
        ":product_name, :identity_source, :identity_confident, :identity_reason, :family, :known_issues, :queries, "
        ":sold_depth, :active_depth, :research_depth, :history_days, :research_unavailable, :research_summary, :notes, "
        ":pages_loaded, :elapsed_ms, :listing_total_cents, :sold_count, :sold_median_cents, :sold_p25_cents, "
        ":sold_p75_cents, :sold_confidence, :shipping_flagged, :shipping_flag_ratio, :for_sale_count, "
        ":cheaper_for_sale, :cheapest_for_sale_cents)"));
    query.bindValue(QStringLiteral(":created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":input"), input);
    query.bindValue(QStringLiteral(":item_id"), textOrNull(item.itemId));
    query.bindValue(QStringLiteral(":status"), runnerStatusToStored(result.status));
    query.bindValue(QStringLiteral(":message"), textOrNull(result.message));
    query.bindValue(QStringLiteral(":app_version"), QString::fromLatin1(version::kString));
    query.bindValue(QStringLiteral(":title"), textOrNull(item.title));
    query.bindValue(QStringLiteral(":price_cents"), optionalValue(item.priceCents));
    query.bindValue(QStringLiteral(":shipping_cents"), optionalValue(item.shippingCents));
    query.bindValue(QStringLiteral(":currency"), textOrNull(item.currency));
    query.bindValue(QStringLiteral(":condition_text"), textOrNull(item.condition));
    query.bindValue(QStringLiteral(":seller_name"), textOrNull(item.sellerName));
    query.bindValue(QStringLiteral(":image_url"), textOrNull(item.imageUrl));
    query.bindValue(QStringLiteral(":specifics"), QString::fromUtf8(QJsonDocument(specifics).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":brand"), textOrNull(identity.brandName));
    query.bindValue(QStringLiteral(":model"), textOrNull(identity.model));
    query.bindValue(QStringLiteral(":product_name"), textOrNull(identity.productName));
    query.bindValue(QStringLiteral(":identity_source"), textOrNull(identity.source));
    query.bindValue(QStringLiteral(":identity_confident"), identity.confident ? 1 : 0);
    query.bindValue(QStringLiteral(":identity_reason"), textOrNull(identity.reason));
    query.bindValue(QStringLiteral(":family"), jsonList(identity.family));
    query.bindValue(QStringLiteral(":known_issues"), jsonList(identity.notes));
    query.bindValue(QStringLiteral(":queries"), jsonList(result.queries));
    query.bindValue(QStringLiteral(":sold_depth"), depths.sold);
    query.bindValue(QStringLiteral(":active_depth"), depths.active);
    query.bindValue(QStringLiteral(":research_depth"), depths.research);
    query.bindValue(QStringLiteral(":history_days"), historyDays);
    query.bindValue(QStringLiteral(":research_unavailable"), result.researchUnavailable ? 1 : 0);
    query.bindValue(QStringLiteral(":research_summary"),
                    QString::fromUtf8(QJsonDocument(result.researchSummary).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":notes"), jsonList(result.notes));
    query.bindValue(QStringLiteral(":pages_loaded"), result.pagesLoaded);
    query.bindValue(QStringLiteral(":elapsed_ms"), result.elapsedMs);
    bindNumbers(query, result, valuation);
    if (!run(query, error))
        return fail();
    const qint64 lookupId = query.lastInsertId().toLongLong();

    QSqlQuery format(db);
    format.prepare(QStringLiteral("UPDATE lookups SET auction = :auction, bids = :bids, buy_it_now = :buy_it_now, "
                                  "best_offer = :best_offer, ends_in = :ends_in, ends_at = :ends_at, auction_end = :auction_end, "
                                  "auction_end_seconds = :auction_end_seconds WHERE id = :id"));
    format.bindValue(QStringLiteral(":auction"), item.auction ? 1 : 0);
    format.bindValue(QStringLiteral(":bids"), optionalInt(item.bids));
    format.bindValue(QStringLiteral(":buy_it_now"), item.buyItNow ? 1 : 0);
    format.bindValue(QStringLiteral(":best_offer"), item.bestOffer ? 1 : 0);
    format.bindValue(QStringLiteral(":ends_in"), textOrNull(item.endsIn));
    format.bindValue(QStringLiteral(":ends_at"), textOrNull(item.endsAt));
    format.bindValue(QStringLiteral(":auction_end"), timeOrNull(item.auctionEnd));
    format.bindValue(QStringLiteral(":auction_end_seconds"), item.auctionEndSeconds ? 1 : 0);
    format.bindValue(QStringLiteral(":id"), lookupId);
    if (!run(format, error))
        return fail();

    QSqlQuery insertRow(db);
    insertRow.prepare(QStringLiteral(
        "INSERT INTO lookup_rows (lookup_id, source, position, item_id, title, condition_text, price_cents, price_text, "
        "shipping_cents, format, bids, seller_name, sold_date, confirmed, total_sold, last_sold_date, bucket, reasons, "
        "included, best_offer, time_left, auction_end_text, auction_end, auction_end_seconds) VALUES (:lookup_id, "
        ":source, :position, :item_id, :title, :condition_text, :price_cents, :price_text, :shipping_cents, :format, "
        ":bids, :seller_name, :sold_date, :confirmed, :total_sold, :last_sold_date, :bucket, :reasons, :included, "
        ":best_offer, :time_left, :auction_end_text, :auction_end, :auction_end_seconds)"));
    for (const Row &row : rows) {
        insertRow.bindValue(QStringLiteral(":lookup_id"), lookupId);
        insertRow.bindValue(QStringLiteral(":source"), rowSourceId(row.source));
        insertRow.bindValue(QStringLiteral(":position"), static_cast<qint64>(row.index));
        insertRow.bindValue(QStringLiteral(":bucket"), bucketId(row.match.bucket));
        insertRow.bindValue(QStringLiteral(":reasons"), jsonList(row.match.reasons));
        insertRow.bindValue(QStringLiteral(":included"), row.included ? 1 : 0);
        if (row.source == Row::Source::Research) {
            const ResearchRecord &record = result.research.at(row.index);
            insertRow.bindValue(QStringLiteral(":item_id"), textOrNull(record.itemId));
            insertRow.bindValue(QStringLiteral(":title"), textOrNull(record.title));
            insertRow.bindValue(QStringLiteral(":condition_text"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":price_cents"), optionalValue(record.avgSoldPriceCents));
            insertRow.bindValue(QStringLiteral(":price_text"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":shipping_cents"), optionalValue(record.avgShippingCents));
            insertRow.bindValue(QStringLiteral(":format"), textOrNull(record.formatText));
            insertRow.bindValue(QStringLiteral(":bids"), QVariant(QMetaType::fromType<int>()));
            insertRow.bindValue(QStringLiteral(":seller_name"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":sold_date"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":confirmed"), 1);
            insertRow.bindValue(QStringLiteral(":total_sold"), optionalInt(record.totalSold));
            insertRow.bindValue(QStringLiteral(":last_sold_date"), textOrNull(record.lastSoldDate));
            insertRow.bindValue(QStringLiteral(":best_offer"), 0);
            insertRow.bindValue(QStringLiteral(":time_left"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":auction_end_text"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":auction_end"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":auction_end_seconds"), 0);
        } else {
            const SearchRecord &record = row.source == Row::Source::Sold ? result.sold.at(row.index) : result.active.at(row.index);
            insertRow.bindValue(QStringLiteral(":item_id"), textOrNull(record.itemId));
            insertRow.bindValue(QStringLiteral(":title"), textOrNull(record.title));
            insertRow.bindValue(QStringLiteral(":condition_text"), textOrNull(record.condition));
            insertRow.bindValue(QStringLiteral(":price_cents"), optionalValue(record.priceCents));
            insertRow.bindValue(QStringLiteral(":price_text"), textOrNull(record.priceText));
            insertRow.bindValue(QStringLiteral(":shipping_cents"), optionalValue(record.shippingCents));
            insertRow.bindValue(QStringLiteral(":format"), textOrNull(record.format));
            insertRow.bindValue(QStringLiteral(":bids"), optionalInt(record.bids));
            insertRow.bindValue(QStringLiteral(":seller_name"), textOrNull(record.sellerName));
            insertRow.bindValue(QStringLiteral(":sold_date"), textOrNull(record.soldDate));
            insertRow.bindValue(QStringLiteral(":confirmed"), record.confirmed ? 1 : 0);
            insertRow.bindValue(QStringLiteral(":total_sold"), QVariant(QMetaType::fromType<int>()));
            insertRow.bindValue(QStringLiteral(":last_sold_date"), QVariant(QMetaType::fromType<QString>()));
            insertRow.bindValue(QStringLiteral(":best_offer"), record.bestOffer ? 1 : 0);
            insertRow.bindValue(QStringLiteral(":time_left"), textOrNull(record.timeLeft));
            insertRow.bindValue(QStringLiteral(":auction_end_text"), textOrNull(record.auctionEndText));
            insertRow.bindValue(QStringLiteral(":auction_end"), timeOrNull(record.auctionEnd));
            insertRow.bindValue(QStringLiteral(":auction_end_seconds"), record.auctionEndSeconds ? 1 : 0);
        }
        if (!run(insertRow, error))
            return fail();
    }
    if (!db.commit()) {
        *error = db.lastError().text();
        return fail();
    }
    return lookupId;
}

bool LookupStore::updateIncluded(qint64 lookupId, const LookupResult &result, const QList<Row> &rows,
                                 const Valuation &valuation, QString *error) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        *error = db.lastError().text();
        return false;
    }
    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE lookup_rows SET included = :included WHERE lookup_id = :lookup_id AND "
                                  "source = :source AND position = :position"));
    for (const Row &row : rows) {
        update.bindValue(QStringLiteral(":included"), row.included ? 1 : 0);
        update.bindValue(QStringLiteral(":lookup_id"), lookupId);
        update.bindValue(QStringLiteral(":source"), rowSourceId(row.source));
        update.bindValue(QStringLiteral(":position"), static_cast<qint64>(row.index));
        if (!run(update, error)) {
            db.rollback();
            return false;
        }
        if (update.numRowsAffected() != 1) {
            *error = QStringLiteral("lookup %1 has no saved row %2/%3").arg(lookupId).arg(rowSourceId(row.source)).arg(row.index);
            db.rollback();
            return false;
        }
    }
    QSqlQuery numbers(db);
    numbers.prepare(QStringLiteral("UPDATE lookups SET %1 WHERE id = :id").arg(QLatin1String(kNumberAssignments)));
    bindNumbers(numbers, result, valuation);
    numbers.bindValue(QStringLiteral(":id"), lookupId);
    if (!run(numbers, error) || numbers.numRowsAffected() != 1) {
        if (error->isEmpty())
            *error = QStringLiteral("there is no saved lookup %1").arg(lookupId);
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        *error = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

std::optional<SavedLookup> LookupStore::load(qint64 lookupId, QString *error) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT created_at, input, item_id, status, message, title, price_cents, shipping_cents, currency, "
        "condition_text, seller_name, image_url, specifics, brand, model, product_name, identity_source, "
        "identity_confident, identity_reason, family, known_issues, queries, sold_depth, active_depth, research_depth, "
        "history_days, research_unavailable, research_summary, notes, pages_loaded, elapsed_ms, auction, bids, buy_it_now, "
        "best_offer, ends_in, ends_at, auction_end, auction_end_seconds FROM lookups WHERE id = ?"));
    query.addBindValue(lookupId);
    if (!run(query, error))
        return std::nullopt;
    if (!query.next()) {
        *error = QStringLiteral("there is no saved lookup %1").arg(lookupId);
        return std::nullopt;
    }
    SavedLookup saved;
    saved.id = lookupId;
    saved.createdAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
    saved.input = query.value(1).toString();
    LookupResult &result = saved.result;
    ItemRecord &item = result.item;
    item.itemId = query.value(2).toString();
    result.status = storedStatusToRunner(query.value(3).toString());
    result.message = query.value(4).toString();
    item.title = query.value(5).toString();
    item.priceCents = optionalCents(query.value(6));
    item.shippingCents = optionalCents(query.value(7));
    item.currency = query.value(8).toString();
    item.condition = query.value(9).toString();
    item.sellerName = query.value(10).toString();
    item.imageUrl = query.value(11).toString();
    for (const QJsonValue &entry : QJsonDocument::fromJson(query.value(12).toString().toUtf8()).array()) {
        const QJsonObject specific = entry.toObject();
        item.specifics.append({specific.value(QStringLiteral("label")).toString(), specific.value(QStringLiteral("value")).toString()});
    }
    Identity &identity = result.identity;
    identity.brandName = query.value(13).toString();
    identity.model = query.value(14).toString();
    identity.productName = query.value(15).toString();
    identity.source = query.value(16).toString();
    identity.confident = query.value(17).toInt() == 1;
    identity.reason = query.value(18).toString();
    identity.family = listFromJson(query.value(19));
    identity.notes = listFromJson(query.value(20));
    result.queries = listFromJson(query.value(21));
    saved.depths = {query.value(22).toInt(), query.value(23).toInt(), query.value(24).toInt()};
    saved.historyDays = query.value(25).toInt();
    result.researchUnavailable = query.value(26).toInt() == 1;
    result.researchSummary = QJsonDocument::fromJson(query.value(27).toString().toUtf8()).object();
    result.notes = listFromJson(query.value(28));
    result.pagesLoaded = query.value(29).toInt();
    result.elapsedMs = query.value(30).toLongLong();
    item.auction = query.value(31).toInt() == 1;
    item.bids = optionalCount(query.value(32));
    item.buyItNow = query.value(33).toInt() == 1;
    item.bestOffer = query.value(34).toInt() == 1;
    item.endsIn = query.value(35).toString();
    item.endsAt = query.value(36).toString();
    item.auctionEnd = optionalTime(query.value(37));
    item.auctionEndSeconds = query.value(38).toInt() == 1;

    QSqlQuery rowsQuery(db);
    rowsQuery.prepare(QStringLiteral(
        "SELECT source, item_id, title, condition_text, price_cents, price_text, shipping_cents, format, bids, "
        "seller_name, sold_date, confirmed, total_sold, last_sold_date, bucket, reasons, included, best_offer, time_left, "
        "auction_end_text, auction_end, auction_end_seconds FROM lookup_rows "
        "WHERE lookup_id = ? ORDER BY CASE source WHEN 'sold' THEN 0 WHEN 'active' THEN 1 ELSE 2 END, position"));
    rowsQuery.addBindValue(lookupId);
    if (!run(rowsQuery, error))
        return std::nullopt;
    while (rowsQuery.next()) {
        const QString source = rowsQuery.value(0).toString();
        const std::optional<Bucket> bucket = bucketFromId(rowsQuery.value(14).toString());
        if (!bucket) {
            *error = QStringLiteral("lookup %1 has a row with an unknown bucket").arg(lookupId);
            return std::nullopt;
        }
        Row row;
        row.match.bucket = *bucket;
        row.match.reasons = listFromJson(rowsQuery.value(15));
        row.included = rowsQuery.value(16).toInt() == 1;
        if (source == QLatin1String("research")) {
            ResearchRecord record;
            record.itemId = rowsQuery.value(1).toString();
            record.title = rowsQuery.value(2).toString();
            record.avgSoldPriceCents = optionalCents(rowsQuery.value(4));
            record.avgShippingCents = optionalCents(rowsQuery.value(6));
            record.formatText = rowsQuery.value(7).toString();
            record.totalSold = optionalCount(rowsQuery.value(12));
            record.lastSoldDate = rowsQuery.value(13).toString();
            row.source = Row::Source::Research;
            row.index = result.research.size();
            result.research.append(record);
        } else {
            SearchRecord record;
            record.itemId = rowsQuery.value(1).toString();
            record.title = rowsQuery.value(2).toString();
            record.condition = rowsQuery.value(3).toString();
            record.priceCents = optionalCents(rowsQuery.value(4));
            record.priceText = rowsQuery.value(5).toString();
            record.shippingCents = optionalCents(rowsQuery.value(6));
            record.format = rowsQuery.value(7).toString();
            record.bids = optionalCount(rowsQuery.value(8));
            record.sellerName = rowsQuery.value(9).toString();
            record.soldDate = rowsQuery.value(10).toString();
            record.confirmed = rowsQuery.value(11).toInt() == 1;
            record.bestOffer = rowsQuery.value(17).toInt() == 1;
            record.timeLeft = rowsQuery.value(18).toString();
            record.auctionEndText = rowsQuery.value(19).toString();
            record.auctionEnd = optionalTime(rowsQuery.value(20));
            record.auctionEndSeconds = rowsQuery.value(21).toInt() == 1;
            const bool sold = source == QLatin1String("sold");
            QList<SearchRecord> &list = sold ? result.sold : result.active;
            row.source = sold ? Row::Source::Sold : Row::Source::Active;
            row.index = list.size();
            list.append(record);
        }
        saved.rows.append(row);
    }
    return saved;
}

std::optional<QList<LookupSummary>> LookupStore::recent(int limit, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT id, created_at, item_id, title, brand, model, status, listing_total_cents, sold_count, "
        "sold_median_cents, sold_confidence, shipping_flagged, shipping_flag_ratio, for_sale_count, cheaper_for_sale, "
        "cheapest_for_sale_cents, condition_text FROM lookups ORDER BY created_at DESC, id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!run(query, error))
        return std::nullopt;
    QList<LookupSummary> list;
    while (query.next()) {
        LookupSummary summary;
        summary.id = query.value(0).toLongLong();
        summary.createdAt = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
        summary.itemId = query.value(2).toString();
        summary.title = query.value(3).toString();
        summary.brand = query.value(4).toString();
        summary.model = query.value(5).toString();
        summary.status = query.value(6).toString();
        summary.listingTotalCents = optionalCents(query.value(7));
        summary.soldCount = query.value(8).toInt();
        summary.soldMedianCents = optionalCents(query.value(9));
        summary.soldConfidence = query.value(10).toString();
        summary.shippingFlagged = query.value(11).toInt() == 1;
        if (!query.value(12).isNull())
            summary.shippingFlagRatio = query.value(12).toDouble();
        summary.forSaleCount = query.value(13).toInt();
        summary.cheaperForSale = query.value(14).toInt();
        summary.cheapestForSaleCents = optionalCents(query.value(15));
        summary.conditionText = query.value(16).toString();
        list.append(summary);
    }
    return list;
}

bool LookupStore::saveUserIdentity(const QString &itemId, const ConfirmedIdentity &identity, std::optional<qint64> lookupId,
                                   QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("INSERT INTO user_identities (saved_at, item_id, brand, model, product_name, lookup_id) "
                                 "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(itemId);
    query.addBindValue(notNull(identity.brand.simplified()));
    query.addBindValue(notNull(identity.model.simplified()));
    query.addBindValue(notNull(identity.productName.simplified()));
    query.addBindValue(optionalValue(lookupId));
    return run(query, error);
}

std::optional<QList<ConfirmedIdentity>> LookupStore::userIdentities(const QString &itemId, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT brand, model, product_name FROM user_identities WHERE item_id = ? ORDER BY id"));
    query.addBindValue(itemId);
    if (!run(query, error))
        return std::nullopt;
    QList<ConfirmedIdentity> list;
    while (query.next())
        list.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
    return list;
}

bool LookupStore::savePhoto(const QString &itemId, const QString &imageUrl, const QByteArray &jpeg, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO listing_photos (item_id, fetched_at, image_url, jpeg) VALUES (?, ?, ?, ?)"));
    query.addBindValue(itemId);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(imageUrl);
    query.addBindValue(jpeg);
    return run(query, error);
}

QByteArray LookupStore::photo(const QString &itemId) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT jpeg FROM listing_photos WHERE item_id = ?"));
    query.addBindValue(itemId);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toByteArray();
}

} // namespace app::lookup
