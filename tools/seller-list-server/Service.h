#pragma once

#include "HttpServer.h"
#include "ItemCheck.h"
#include "ServerStore.h"
#include "sellerlist/Protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace sellerserver {

// The seller list service (DESIGN 3.7): report and dispute intake, the abuse
// limits, the item check, and the signed list.
//
// GET  /v1/list    {"authorization", "revocations", "list"} (app::sellerlist::checkBundle)
// GET  /v1/keys    {"authorization", "revocations"}         (app::sellerlist::checkKeys)
// POST /v1/report  a sealed envelope (label "report") holding a Report
// POST /v1/dispute a sealed envelope (label "dispute") holding a Dispute
//
// A report goes live automatically (owner decision) after its checks:
//   - the envelope opens with the server's box key, the report is well
//     formed and its installation signature verifies;
//   - createdAt within the last day (and not more than 5 minutes ahead);
//   - the server's total for the last 24 hours is under maxReportsPerDay
//     (else "busy", nothing stored);
//   - this installation has not reported this seller before (refused);
//   - this installation sent fewer than perInstallPerDay in 24 hours (refused);
//   - fewer than sellerBurst reports for this seller in 24 hours, else HELD;
//   - the item check confirms the item page and the seller, else HELD.
// The owner publishes or removes held reports, and removes any report on
// request (moderation commands); the next list follows within listCheckMs.
class Service : public QObject {
    Q_OBJECT
public:
    struct Limits {
        int perInstallPerDay = 5;
        int sellerBurst = 3;
        int maxReportsPerDay = 500;
        int maxDisputesPerDay = 200;
        int maxOpenDisputes = 2000;
        int listCheckMs = 60 * 1000;
    };

    struct Keys {
        app::sellerlist::SignKeys sign;
        app::sellerlist::BoxKeys box;
        app::sellerlist::SignPublic master; // the owner's master public keys
        QJsonObject authorization;  // master-signed
        QJsonArray revocations;     // master-signed
    };

    using Clock = std::function<QDateTime()>;

    // `store` must be open; `listFile` is where the signed bundle is kept.
    Service(ServerStore *store, Keys keys, ItemPageFetcher::Settings fetcher, Limits limits, QString listFile,
            QObject *parent = nullptr);
    ~Service() override;

    // Checks the authorization as the app does (master-signed, valid now, not
    // revoked) and that it names this server's keys,
    // queues the item check of every pending report and publishes the first
    // list. False and *error when the service must not start.
    bool start(QString *error);

    HttpServer::Response handle(const HttpServer::Request &request);

    // Re-reads the published reports and signs a new list when they changed
    // (or when `force`); its sequence is the last one + 1, at least the time
    // in seconds since 1970 (a lost database never makes the apps refuse the
    // next list as old). False and *error on failure.
    bool publish(bool force, QString *error);

    QByteArray bundle() const { return m_bundle; }
    void setClock(Clock clock) { m_clock = std::move(clock); }

signals:
    // A report's state changed (tests wait on it).
    void reportSettled(const QString &reportId, sellerserver::ServerStore::Status status);
    void published(qint64 sequence);
    void problem(const QString &message);

private:
    HttpServer::Response receiveReport(const QByteArray &envelope);
    HttpServer::Response receiveDispute(const QByteArray &envelope);
    void onItemChecked(const QString &reportId, const sellerserver::ItemReading &reading);
    QDateTime now() const;

    ServerStore *m_store;
    Keys m_keys;
    app::sellerlist::Authorization m_authorization;
    Limits m_limits;
    QString m_listFile;
    ItemPageFetcher *m_fetcher;
    QTimer *m_listTimer;
    QJsonArray m_listedSellers;
    QByteArray m_bundle;
    QByteArray m_keysDocument;
    Clock m_clock;
};

} // namespace sellerserver
