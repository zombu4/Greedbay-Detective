#include "Service.h"

#include "KeyFiles.h"
#include "sellerlist/Trust.h"

#include <QJsonDocument>
#include <QMap>
#include <QTimer>

#include <algorithm>

namespace sellerserver {

namespace sl = app::sellerlist;

namespace {

constexpr qint64 kDaySeconds = 24 * 60 * 60;
constexpr qint64 kFutureLeewaySeconds = 300;

HttpServer::Response answer(int status, const QString &result, const QString &why = QString())
{
    QJsonObject object{{QStringLiteral("result"), result}};
    if (!why.isEmpty())
        object.insert(QStringLiteral("why"), why);
    return {status, "application/json", QJsonDocument(object).toJson(QJsonDocument::Compact)};
}

} // namespace

Service::Service(ServerStore *store, Keys keys, Limits limits, QString listFile, QObject *parent)
    : QObject(parent),
      m_store(store),
      m_keys(std::move(keys)),
      m_limits(limits),
      m_listFile(std::move(listFile)),
      m_listTimer(new QTimer(this)),
      m_clock([] { return QDateTime::currentDateTimeUtc(); })
{
    connect(m_listTimer, &QTimer::timeout, this, [this] {
        QString error;
        if (!publish(false, &error))
            emit problem(error);
    });
}

Service::~Service()
{
    wipe(&m_keys.sign);
    wipe(&m_keys.box);
}

QDateTime Service::now() const { return m_clock(); }

bool Service::start(QString *error)
{
    const std::optional<sl::Authorization> unchecked
        = sl::Authorization::fromBody(m_keys.authorization.value(QStringLiteral("body")).toObject());
    if (!unchecked) {
        *error = QStringLiteral("the authorization file is not readable");
        return false;
    }
    // The same checks the app makes: signed by the master, valid now, not
    // revoked.
    QString why;
    const std::optional<sl::Authorization> authorization
        = sl::checkKeys(QJsonObject{{QStringLiteral("authorization"), m_keys.authorization},
                                    {QStringLiteral("revocations"), m_keys.revocations}},
                        sl::MasterInfo{m_keys.master, unchecked->host}, now(), &why);
    if (!authorization) {
        *error = QStringLiteral("the authorization does not hold: %1").arg(why);
        return false;
    }
    if (!(authorization->serverSign == m_keys.sign.pub) || !(authorization->serverBox == m_keys.box.pub)) {
        *error = QStringLiteral("the authorization is for other keys than this server's");
        return false;
    }
    m_authorization = *authorization;
    m_keysDocument = QJsonDocument(QJsonObject{{QStringLiteral("authorization"), m_keys.authorization},
                                               {QStringLiteral("revocations"), m_keys.revocations}})
                         .toJson(QJsonDocument::Compact);

    const std::optional<QList<ServerStore::StoredReport>> pending = m_store->reports(ServerStore::Status::Pending, error);
    if (!pending)
        return false;
    // The server no longer loads eBay pages (owner decision a, 2026-10-21):
    // a report an older version left waiting for that check goes to the
    // owner's queue.
    for (const ServerStore::StoredReport &stored : *pending) {
        if (!m_store->setStatus(stored.report.id, ServerStore::Status::Held,
                                QStringLiteral("received before the app's own item check; look at it"), error))
            return false;
    }
    if (!publish(true, error))
        return false;
    m_listTimer->start(m_limits.listCheckMs);
    return true;
}

HttpServer::Response Service::handle(const HttpServer::Request &request)
{
    if (request.path == "/v1/list" || request.path == "/v1/keys") {
        if (request.method != "GET")
            return answer(405, QStringLiteral("refused"), QStringLiteral("GET only"));
        return {200, "application/json", request.path == "/v1/list" ? m_bundle : m_keysDocument};
    }
    if (request.path == "/v1/report" || request.path == "/v1/dispute") {
        if (request.method != "POST")
            return answer(405, QStringLiteral("refused"), QStringLiteral("POST only"));
        return request.path == "/v1/report" ? receiveReport(request.body) : receiveDispute(request.body);
    }
    return answer(404, QStringLiteral("refused"), QStringLiteral("no such path"));
}

HttpServer::Response Service::receiveReport(const QByteArray &envelope)
{
    const std::optional<QByteArray> opened = sl::open(envelope, m_keys.box, QStringLiteral("report"));
    if (!opened)
        return answer(400, QStringLiteral("refused"), QStringLiteral("the report could not be opened"));
    const QJsonDocument document = QJsonDocument::fromJson(*opened);
    const std::optional<sl::Report> report = document.isObject() ? sl::Report::fromJson(document.object()) : std::nullopt;
    if (!report)
        return answer(400, QStringLiteral("refused"), QStringLiteral("the report is not readable"));
    const QString wrong = report->problem();
    if (!wrong.isEmpty())
        return answer(400, QStringLiteral("refused"), wrong);
    const QDateTime time = now();
    if (report->createdAt < time.addSecs(-kDaySeconds) || report->createdAt > time.addSecs(kFutureLeewaySeconds))
        return answer(400, QStringLiteral("refused"), QStringLiteral("the report's time is not plausible"));

    QString error;
    const QDateTime dayAgo = time.addSecs(-kDaySeconds);
    const std::optional<int> total = m_store->reportsSince(dayAgo, &error);
    const std::optional<int> sameSeller = m_store->reportsBy(report->installKey, report->sellerId, &error);
    const std::optional<int> byInstall = m_store->reportsBySince(report->installKey, dayAgo, &error);
    const std::optional<int> forSeller = m_store->reportsForSince(report->sellerId, dayAgo, &error);
    if (!total || !sameSeller || !byInstall || !forSeller) {
        emit problem(error);
        return answer(500, QStringLiteral("busy"), QStringLiteral("the server could not store the report"));
    }
    if (*total >= m_limits.maxReportsPerDay)
        return answer(503, QStringLiteral("busy"), QStringLiteral("the server takes no more reports today"));
    if (*sameSeller > 0)
        return answer(409, QStringLiteral("refused"), QStringLiteral("this installation already reported this seller"));
    if (*byInstall >= m_limits.perInstallPerDay)
        return answer(429, QStringLiteral("refused"), QStringLiteral("too many reports from this installation today"));

    // Published at once (owner decision a, 2026-10-21), with the reporter's
    // app's item check as "item checked"; only a burst for one seller is held.
    ServerStore::StoredReport stored;
    stored.report = {sl::fingerprint(report->signedBytes()), report->reason, report->text.trimmed(), report->month,
                     report->itemCheckedInApp};
    stored.itemId = report->itemId;
    stored.sellerId = report->sellerId;
    stored.installKey = report->installKey;
    stored.receivedAt = time;
    const bool burst = *forSeller >= m_limits.sellerBurst;
    stored.status = burst ? ServerStore::Status::Held : ServerStore::Status::Published;
    if (burst)
        stored.holdReason = QStringLiteral("many reports for this seller in 24 hours");
    if (!m_store->addReport(stored, &error)) {
        emit problem(error);
        return answer(500, QStringLiteral("busy"), QStringLiteral("the server could not store the report"));
    }
    emit reportSettled(stored.report.id, stored.status);
    if (burst)
        return answer(202, QStringLiteral("held"), QStringLiteral("held for the list owner to look at"));
    if (!publish(false, &error))
        emit problem(error);
    return answer(202, QStringLiteral("received"));
}

HttpServer::Response Service::receiveDispute(const QByteArray &envelope)
{
    const std::optional<QByteArray> opened = sl::open(envelope, m_keys.box, QStringLiteral("dispute"));
    if (!opened)
        return answer(400, QStringLiteral("refused"), QStringLiteral("the dispute could not be opened"));
    const QJsonDocument document = QJsonDocument::fromJson(*opened);
    const std::optional<sl::Dispute> dispute = document.isObject() ? sl::Dispute::fromJson(document.object()) : std::nullopt;
    if (!dispute)
        return answer(400, QStringLiteral("refused"), QStringLiteral("the dispute is not readable"));
    const QString wrong = dispute->problem();
    if (!wrong.isEmpty())
        return answer(400, QStringLiteral("refused"), wrong);
    QString error;
    const QDateTime time = now();
    const std::optional<int> today = m_store->disputesSince(time.addSecs(-kDaySeconds), &error);
    const std::optional<int> open = m_store->openDisputeCount(&error);
    if (!today || !open) {
        emit problem(error);
        return answer(500, QStringLiteral("busy"), QStringLiteral("the server could not store the dispute"));
    }
    if (*today >= m_limits.maxDisputesPerDay || *open >= m_limits.maxOpenDisputes)
        return answer(503, QStringLiteral("busy"), QStringLiteral("the server takes no more disputes for now"));
    sl::Dispute clean = *dispute;
    clean.text = clean.text.trimmed();
    if (!m_store->addDispute(clean, time, &error)) {
        emit problem(error);
        return answer(500, QStringLiteral("busy"), QStringLiteral("the server could not store the dispute"));
    }
    return answer(202, QStringLiteral("received"));
}

bool Service::publish(bool force, QString *error)
{
    const std::optional<QList<ServerStore::StoredReport>> reports = m_store->reports(ServerStore::Status::Published, error);
    if (!reports)
        return false;
    QMap<QString, sl::ListedSeller> bySeller; // by lower-case id: one order on every run
    for (const ServerStore::StoredReport &stored : *reports) {
        sl::ListedSeller &seller = bySeller[stored.sellerId.toLower()];
        if (seller.sellerId.isEmpty())
            seller.sellerId = stored.sellerId;
        seller.reports.append(stored.report);
    }
    sl::SellerList list;
    list.sellers = bySeller.values();
    const QJsonArray sellers = list.toBody().value(QStringLiteral("sellers")).toArray();
    if (!force && sellers == m_listedSellers)
        return true;

    const std::optional<qint64> last = m_store->listSequence(error);
    if (!last)
        return false;
    // At least the time in seconds: a server whose database was lost (the
    // last sequence with it) still signs lists newer than any the apps have.
    list.sequence = std::max(*last + 1, now().toSecsSinceEpoch());
    list.signedAt = now();
    list.authorizationSequence = m_authorization.sequence;
    const std::optional<QJsonObject> signedList = sl::signDocument(list.toBody(), m_keys.sign);
    if (!signedList) {
        *error = QStringLiteral("signing the list failed");
        return false;
    }
    const QByteArray bundle = QJsonDocument(QJsonObject{{QStringLiteral("authorization"), m_keys.authorization},
                                                        {QStringLiteral("revocations"), m_keys.revocations},
                                                        {QStringLiteral("list"), *signedList}})
                                  .toJson(QJsonDocument::Compact);
    // The sequence is stored first: a list file is never served with a
    // sequence the database could hand out again.
    if (!m_store->setListSequence(list.sequence, error) || !replaceFile(m_listFile, bundle, error))
        return false;
    m_bundle = bundle;
    m_listedSellers = sellers;
    emit published(list.sequence);
    return true;
}

} // namespace sellerserver
