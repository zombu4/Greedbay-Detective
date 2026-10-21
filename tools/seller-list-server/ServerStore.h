#pragma once

#include "sellerlist/Protocol.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace sellerserver {

// The server's database (SQLite, one file in the server's data folder):
// reports (pending the item check, published, held for the owner, removed),
// disputes, and the last
// published list's sequence. Nothing that identifies a reporter is stored
// except the installation key (for the abuse limits), never published.
class ServerStore {
public:
    enum class Status { Pending, Published, Held, Removed };

    struct StoredReport {
        app::sellerlist::PublishedReport report;
        QString sellerId;
        QByteArray installKey;
        QDateTime receivedAt;
        Status status = Status::Pending;
        QString holdReason;
    };

    struct StoredDispute {
        qint64 id = 0;
        app::sellerlist::Dispute dispute;
        QDateTime receivedAt;
        bool closed = false;
    };

    // Opens (and creates) the database at `path`; false and *error on
    // failure. `connectionName` must be unique in the process.
    bool open(const QString &path, const QString &connectionName, QString *error);
    void close();

    bool addReport(const StoredReport &report, QString *error);
    // Sets the status (and the hold reason, empty when not held); `verified`
    // is set only when given (the item check's outcome).
    bool setStatus(const QString &reportId, Status status, const QString &holdReason, QString *error,
                   std::optional<bool> verified = std::nullopt);
    std::optional<StoredReport> report(const QString &reportId, QString *error) const;
    // Every report of a status, oldest first.
    std::optional<QList<StoredReport>> reports(Status status, QString *error) const;
    // Reports (any status) of this installation for this seller.
    std::optional<int> reportsBy(const QByteArray &installKey, const QString &sellerId, QString *error) const;
    // Reports (any status) of this installation since `since`.
    std::optional<int> reportsBySince(const QByteArray &installKey, const QDateTime &since, QString *error) const;
    // Reports (not removed) for this seller since `since`.
    std::optional<int> reportsForSince(const QString &sellerId, const QDateTime &since, QString *error) const;

    // Every report (any status) received since `since` (the server's daily cap).
    std::optional<int> reportsSince(const QDateTime &since, QString *error) const;

    bool addDispute(const app::sellerlist::Dispute &dispute, const QDateTime &receivedAt, QString *error);
    std::optional<QList<StoredDispute>> openDisputes(QString *error) const;
    bool closeDispute(qint64 id, QString *error);
    std::optional<int> disputesSince(const QDateTime &since, QString *error) const;
    std::optional<int> openDisputeCount(QString *error) const;

    // The last published list's sequence (0 before the first).
    std::optional<qint64> listSequence(QString *error) const;
    bool setListSequence(qint64 sequence, QString *error);

    static QString statusId(Status status);

private:
    // A count(*) query with at most one bound text value (none when null).
    std::optional<int> count(const QString &sql, const QString &value, QString *error) const;

    QString m_connection;
};

} // namespace sellerserver
