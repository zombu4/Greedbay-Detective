#include "lookup/SellerStore.h"

#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace app::lookup {

namespace {

constexpr qsizetype kMaxNoteLength = 2000;

SellerMark markFrom(const QSqlQuery &query)
{
    return SellerMark{query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                      QDate::fromString(query.value(3).toString(), Qt::ISODate)};
}

} // namespace

SellerStore::SellerStore(QString connectionName) : m_connectionName(std::move(connectionName)) {}

bool SellerStore::mark(const SellerMark &mark, QString *error) const
{
    if (!isSellerId(mark.sellerId)) {
        *error = QStringLiteral("\"%1\" is not an eBay user id").arg(mark.sellerId);
        return false;
    }
    if (!isMarkReason(mark.reason)) {
        *error = QStringLiteral("\"%1\" is not a reason the app knows").arg(mark.reason);
        return false;
    }
    if (mark.note.size() > kMaxNoteLength) {
        *error = QStringLiteral("the note is longer than %1 characters").arg(kMaxNoteLength);
        return false;
    }
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QDate day = mark.markedOn.isValid() ? mark.markedOn : QDate::currentDate();
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    // A new seller gets the given date; an existing mark keeps its first one.
    query.prepare(QStringLiteral("INSERT INTO marked_sellers (seller_id, reason, note, marked_on, updated_at) "
                                 "VALUES (:seller_id, :reason, :note, :marked_on, :updated_at) "
                                 "ON CONFLICT(seller_id) DO UPDATE SET reason = excluded.reason, note = excluded.note, "
                                 "updated_at = excluded.updated_at"));
    query.bindValue(QStringLiteral(":seller_id"), mark.sellerId);
    query.bindValue(QStringLiteral(":reason"), mark.reason);
    // An empty note is stored as '' (the column is NOT NULL; a null QString
    // would bind as NULL).
    query.bindValue(QStringLiteral(":note"), mark.note.isNull() ? QStringLiteral("") : mark.note);
    query.bindValue(QStringLiteral(":marked_on"), day.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":updated_at"), now);
    if (!query.exec()) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

bool SellerStore::unmark(const QString &sellerId, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("DELETE FROM marked_sellers WHERE seller_id = :seller_id"));
    query.bindValue(QStringLiteral(":seller_id"), sellerId);
    if (!query.exec()) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

std::optional<SellerMark> SellerStore::markOf(const QString &sellerId, QString *error) const
{
    if (sellerId.isEmpty())
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT seller_id, reason, note, marked_on FROM marked_sellers WHERE seller_id = :seller_id"));
    query.bindValue(QStringLiteral(":seller_id"), sellerId);
    if (!query.exec()) {
        *error = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return markFrom(query);
}

std::optional<QList<SellerMark>> SellerStore::marks(QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral("SELECT seller_id, reason, note, marked_on FROM marked_sellers "
                                   "ORDER BY updated_at DESC, seller_id"))) {
        *error = query.lastError().text();
        return std::nullopt;
    }
    QList<SellerMark> result;
    while (query.next())
        result.append(markFrom(query));
    return result;
}

bool SellerStore::saveFeedback(const SellerFeedback &feedback, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("INSERT INTO seller_feedback (seller_id, kind, fetched_at, data) "
                                 "VALUES (:seller_id, :kind, :fetched_at, :data) "
                                 "ON CONFLICT(seller_id, kind) DO UPDATE SET fetched_at = excluded.fetched_at, data = excluded.data"));
    query.bindValue(QStringLiteral(":seller_id"), feedback.sellerId);
    query.bindValue(QStringLiteral(":kind"), feedback.kind);
    query.bindValue(QStringLiteral(":fetched_at"), feedback.fetchedAt.toUTC().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":data"), QString::fromUtf8(QJsonDocument(feedback.toJson()).toJson(QJsonDocument::Compact)));
    if (!query.exec()) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

std::optional<SellerFeedback> SellerStore::feedback(const QString &sellerId, const QString &kind, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT fetched_at, data FROM seller_feedback WHERE seller_id = :seller_id AND kind = :kind"));
    query.bindValue(QStringLiteral(":seller_id"), sellerId);
    query.bindValue(QStringLiteral(":kind"), kind);
    if (!query.exec()) {
        *error = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    std::optional<SellerFeedback> feedback = SellerFeedback::fromJson(QJsonDocument::fromJson(query.value(1).toString().toUtf8()).object());
    if (feedback)
        feedback->fetchedAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
    return feedback;
}

} // namespace app::lookup
