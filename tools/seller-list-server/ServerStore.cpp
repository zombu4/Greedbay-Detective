#include "ServerStore.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace sellerserver {

namespace {

// WAL and a busy timeout: the owner's moderation commands open the same file
// while the server runs.
const char *const kSchema[] = {
    "PRAGMA busy_timeout = 5000",
    "PRAGMA journal_mode = WAL",
    "CREATE TABLE IF NOT EXISTS reports ("
    " id TEXT PRIMARY KEY,"
    " seller_id TEXT NOT NULL COLLATE NOCASE,"
    " reason TEXT NOT NULL,"
    " text TEXT NOT NULL,"
    " item_id TEXT NOT NULL,"
    " month TEXT NOT NULL,"
    " verified INTEGER NOT NULL CHECK (verified IN (0, 1)),"
    " install_key BLOB NOT NULL,"
    " received_at TEXT NOT NULL,"
    " status TEXT NOT NULL CHECK (status IN ('pending', 'published', 'held', 'removed')),"
    " hold_reason TEXT NOT NULL DEFAULT '')",
    "CREATE INDEX IF NOT EXISTS reports_seller ON reports(seller_id)",
    "CREATE INDEX IF NOT EXISTS reports_install ON reports(install_key)",
    "CREATE TABLE IF NOT EXISTS disputes ("
    " id INTEGER PRIMARY KEY,"
    " seller_id TEXT NOT NULL,"
    " report_id TEXT NOT NULL,"
    " text TEXT NOT NULL,"
    " received_at TEXT NOT NULL,"
    " closed INTEGER NOT NULL DEFAULT 0 CHECK (closed IN (0, 1)))",
    "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
};

ServerStore::Status statusOf(const QString &id)
{
    if (id == QLatin1String("published"))
        return ServerStore::Status::Published;
    if (id == QLatin1String("removed"))
        return ServerStore::Status::Removed;
    if (id == QLatin1String("pending"))
        return ServerStore::Status::Pending;
    return ServerStore::Status::Held;
}

QString timeText(const QDateTime &time) { return time.toUTC().toString(Qt::ISODateWithMs); }

bool run(QSqlQuery &query, QString *error)
{
    if (query.exec())
        return true;
    *error = query.lastError().text();
    return false;
}

ServerStore::StoredReport reportFrom(const QSqlQuery &query)
{
    ServerStore::StoredReport stored;
    stored.report.id = query.value(0).toString();
    stored.sellerId = query.value(1).toString();
    stored.report.reason = query.value(2).toString();
    stored.report.text = query.value(3).toString();
    stored.report.itemId = query.value(4).toString();
    stored.report.month = query.value(5).toString();
    stored.report.verifiedPurchase = query.value(6).toInt() == 1;
    stored.installKey = query.value(7).toByteArray();
    stored.receivedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
    stored.status = statusOf(query.value(9).toString());
    stored.holdReason = query.value(10).toString();
    return stored;
}

const char kReportColumns[] = "id, seller_id, reason, text, item_id, month, verified, install_key, received_at, status, hold_reason";

} // namespace

QString ServerStore::statusId(Status status)
{
    switch (status) {
    case Status::Pending:
        return QStringLiteral("pending");
    case Status::Published:
        return QStringLiteral("published");
    case Status::Held:
        return QStringLiteral("held");
    case Status::Removed:
        return QStringLiteral("removed");
    }
    return {};
}

std::optional<int> ServerStore::count(const QString &sql, const QString &value, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(sql);
    if (!value.isNull())
        query.addBindValue(value);
    if (!run(query, error) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

bool ServerStore::open(const QString &path, const QString &connectionName, QString *error)
{
    m_connection = connectionName;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        *error = db.lastError().text();
        return false;
    }
    QSqlQuery query(db);
    for (const char *statement : kSchema) {
        if (!query.exec(QString::fromLatin1(statement))) {
            *error = query.lastError().text();
            return false;
        }
    }
    return true;
}

void ServerStore::close()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

bool ServerStore::addReport(const StoredReport &stored, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("INSERT INTO reports (id, seller_id, reason, text, item_id, month, verified, install_key, "
                                 "received_at, status, hold_reason) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(stored.report.id);
    query.addBindValue(stored.sellerId);
    query.addBindValue(stored.report.reason);
    query.addBindValue(stored.report.text);
    query.addBindValue(stored.report.itemId);
    query.addBindValue(stored.report.month);
    query.addBindValue(stored.report.verifiedPurchase ? 1 : 0);
    query.addBindValue(stored.installKey);
    query.addBindValue(timeText(stored.receivedAt));
    query.addBindValue(statusId(stored.status));
    query.addBindValue(stored.holdReason.isNull() ? QStringLiteral("") : stored.holdReason);
    return run(query, error);
}

bool ServerStore::setStatus(const QString &reportId, Status status, const QString &holdReason, QString *error,
                            std::optional<bool> verified)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(verified ? QStringLiteral("UPDATE reports SET status = ?, hold_reason = ?, verified = ? WHERE id = ?")
                           : QStringLiteral("UPDATE reports SET status = ?, hold_reason = ? WHERE id = ?"));
    query.addBindValue(statusId(status));
    query.addBindValue(holdReason.isNull() ? QStringLiteral("") : holdReason);
    if (verified)
        query.addBindValue(*verified ? 1 : 0);
    query.addBindValue(reportId);
    if (!run(query, error))
        return false;
    if (query.numRowsAffected() != 1) {
        *error = QStringLiteral("no report %1").arg(reportId);
        return false;
    }
    return true;
}

std::optional<ServerStore::StoredReport> ServerStore::report(const QString &reportId, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM reports WHERE id = ?").arg(QLatin1String(kReportColumns)));
    query.addBindValue(reportId);
    if (!run(query, error) || !query.next())
        return std::nullopt;
    return reportFrom(query);
}

std::optional<QList<ServerStore::StoredReport>> ServerStore::reports(Status status, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM reports WHERE status = ? ORDER BY received_at, id").arg(QLatin1String(kReportColumns)));
    query.addBindValue(statusId(status));
    if (!run(query, error))
        return std::nullopt;
    QList<StoredReport> list;
    while (query.next())
        list.append(reportFrom(query));
    return list;
}

std::optional<int> ServerStore::reportsBy(const QByteArray &installKey, const QString &sellerId, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT count(*) FROM reports WHERE install_key = ? AND seller_id = ?"));
    query.addBindValue(installKey);
    query.addBindValue(sellerId);
    if (!run(query, error) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

std::optional<int> ServerStore::reportsBySince(const QByteArray &installKey, const QDateTime &since, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT count(*) FROM reports WHERE install_key = ? AND received_at >= ?"));
    query.addBindValue(installKey);
    query.addBindValue(timeText(since));
    if (!run(query, error) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

std::optional<int> ServerStore::reportsForSince(const QString &sellerId, const QDateTime &since, QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT count(*) FROM reports WHERE seller_id = ? AND received_at >= ? AND status != 'removed'"));
    query.addBindValue(sellerId);
    query.addBindValue(timeText(since));
    if (!run(query, error) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

std::optional<int> ServerStore::reportsSince(const QDateTime &since, QString *error) const
{
    return count(QStringLiteral("SELECT count(*) FROM reports WHERE received_at >= ?"), timeText(since), error);
}

bool ServerStore::addDispute(const app::sellerlist::Dispute &dispute, const QDateTime &receivedAt, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("INSERT INTO disputes (seller_id, report_id, text, received_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(dispute.sellerId);
    query.addBindValue(dispute.reportId.isNull() ? QStringLiteral("") : dispute.reportId);
    query.addBindValue(dispute.text);
    query.addBindValue(timeText(receivedAt));
    return run(query, error);
}

std::optional<QList<ServerStore::StoredDispute>> ServerStore::openDisputes(QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT id, seller_id, report_id, text, received_at FROM disputes WHERE closed = 0 ORDER BY id"));
    if (!run(query, error))
        return std::nullopt;
    QList<StoredDispute> list;
    while (query.next()) {
        StoredDispute stored;
        stored.id = query.value(0).toLongLong();
        stored.dispute = {query.value(1).toString(), query.value(2).toString(), query.value(3).toString()};
        stored.receivedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        list.append(stored);
    }
    return list;
}

bool ServerStore::closeDispute(qint64 id, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("UPDATE disputes SET closed = 1 WHERE id = ?"));
    query.addBindValue(id);
    return run(query, error);
}

std::optional<int> ServerStore::disputesSince(const QDateTime &since, QString *error) const
{
    return count(QStringLiteral("SELECT count(*) FROM disputes WHERE received_at >= ?"), timeText(since), error);
}

std::optional<int> ServerStore::openDisputeCount(QString *error) const
{
    return count(QStringLiteral("SELECT count(*) FROM disputes WHERE closed = 0"), {}, error);
}

std::optional<qint64> ServerStore::listSequence(QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = 'list_sequence'"));
    if (!run(query, error))
        return std::nullopt;
    return query.next() ? query.value(0).toLongLong() : 0;
}

bool ServerStore::setListSequence(qint64 sequence, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("INSERT INTO meta (key, value) VALUES ('list_sequence', ?) "
                                 "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(QString::number(sequence));
    return run(query, error);
}

} // namespace sellerserver
