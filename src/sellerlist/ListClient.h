#pragma once

#include "sellerlist/Protocol.h"
#include "sellerlist/Trust.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace app::sellerlist {

// The app's side of the shared seller list (DESIGN 3.7): fetches the signed
// list, keeps the last verified one in the work folder's sharedlist/, and
// sends reports and disputes sealed to the server's key.
//
// Never a direct connection: every request goes to http://<the master's
// onion address>/ and only while the network manager's proxy is a SOCKS5
// proxy on 127.0.0.1 (the app's own tor, TorRunner). The only other target
// is a test server on 127.0.0.1 (setLocalTestServer, used by tests).
//
// Files in `folder`:
//   list.json     {"bundle": <the server's bundle>, "fetchedAt": time}
//   install.json  this installation's report key (owner-only file); made on
//                 the first report
class ListClient : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kMaxListBytes = 8 * 1024 * 1024;
    static constexpr qint64 kMaxReplyBytes = 64 * 1024;
    static constexpr int kTimeoutMs = 120 * 1000;

    ListClient(QNetworkAccessManager *network, MasterInfo master, QString folder, QObject *parent = nullptr);

    // Tests only: talk to http://127.0.0.1:<port> without a proxy.
    void setLocalTestServer(quint16 port);

    // Reads list.json and checks it again (as at fetch time, against `now`);
    // false and *why when there is none or it no longer holds.
    bool loadCache(const QDateTime &now, QString *why);
    const std::optional<TrustedList> &list() const { return m_list; }
    QDateTime fetchedAt() const { return m_fetchedAt; }

    bool busy() const { return m_reply != nullptr; }

    // GET /v1/list, checkBundle() against the cached list's sequence, then
    // list.json. listFetched(true, "") or listFetched(false, why).
    void fetchList();
    // Fills in createdAt, the installation key and its signature, then GET
    // /v1/keys, checkKeys(), seal and POST /v1/report.
    // reportSent(ok, result, why): result is the server's "received", "held",
    // "refused" or "busy".
    void sendReport(Report report);
    void sendDispute(const Dispute &dispute);

signals:
    void listFetched(bool ok, const QString &why);
    void reportSent(bool ok, const QString &result, const QString &why);
    void disputeSent(bool ok, const QString &result, const QString &why);

private:
    enum class Job { None, List, KeysForReport, Report, KeysForDispute, Dispute };

    QUrl url(const QString &path) const;
    // Empty when a request may go out, else why not.
    QString transportProblem() const;
    void get(Job job, const QString &path, qint64 maxBytes);
    void post(Job job, const QString &path, const QByteArray &body);
    void onFinished();
    void finish(Job job, bool ok, const QString &result, const QString &why);
    void sealAndPost(const QJsonObject &keys);
    std::optional<InstallKey> installKey(QString *why);

    QNetworkAccessManager *m_network;
    MasterInfo m_master;
    QString m_folder;
    quint16 m_testPort = 0;
    std::optional<TrustedList> m_list;
    QDateTime m_fetchedAt;
    QNetworkReply *m_reply = nullptr;
    Job m_job = Job::None;
    qint64 m_maxBytes = 0;
    std::optional<Report> m_pendingReport;
    std::optional<Dispute> m_pendingDispute;
};

} // namespace app::sellerlist
