#include "sellerlist/ListClient.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace app::sellerlist {

namespace {

const QString kListFile = QStringLiteral("list.json");
const QString kInstallFile = QStringLiteral("install.json");

QString timeText(const QDateTime &time) { return time.toUTC().toString(Qt::ISODateWithMs); }

} // namespace

ListClient::ListClient(QNetworkAccessManager *network, MasterInfo master, QString folder, QObject *parent)
    : QObject(parent), m_network(network), m_master(std::move(master)), m_folder(std::move(folder))
{
}

void ListClient::setLocalTestServer(quint16 port) { m_testPort = port; }

QUrl ListClient::url(const QString &path) const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    if (m_testPort != 0) {
        url.setHost(QStringLiteral("127.0.0.1"));
        url.setPort(m_testPort);
    } else {
        url.setHost(m_master.onion);
    }
    url.setPath(path);
    return url;
}

QString ListClient::transportProblem() const
{
    if (m_testPort != 0)
        return {};
    if (!isOnionAddress(m_master.onion))
        return tr("the shared list is not set up in this build");
    const QNetworkProxy proxy = m_network->proxy();
    if (proxy.type() != QNetworkProxy::Socks5Proxy || proxy.hostName() != QLatin1String("127.0.0.1") || proxy.port() == 0)
        return tr("Tor is not running; the app never connects directly");
    return {};
}

bool ListClient::loadCache(const QDateTime &now, QString *why)
{
    QFile file(QDir(m_folder).filePath(kListFile));
    if (!file.exists()) {
        *why = tr("no shared list yet");
        return false;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxListBytes + 4096) {
        *why = tr("the saved shared list cannot be read");
        return false;
    }
    const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    const QDateTime fetched = QDateTime::fromString(saved.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODateWithMs);
    QString reason;
    const std::optional<TrustedList> list = checkBundle(saved.value(QStringLiteral("bundle")).toObject(), m_master, 0, now, &reason);
    if (!list || !fetched.isValid()) {
        *why = tr("the saved shared list no longer holds: %1").arg(reason.isEmpty() ? tr("no time") : reason);
        return false;
    }
    m_list = list;
    m_fetchedAt = fetched;
    return true;
}

void ListClient::fetchList() { get(Job::List, QStringLiteral("/v1/list"), kMaxListBytes); }

void ListClient::sendReport(Report report)
{
    if (busy()) {
        emit reportSent(false, QStringLiteral("refused"), tr("another request is still running"));
        return;
    }
    QString why;
    std::optional<InstallKey> key = installKey(&why);
    if (!key) {
        emit reportSent(false, QStringLiteral("refused"), why);
        return;
    }
    report.createdAt = QDateTime::currentDateTimeUtc();
    report.installKey = key->pub;
    report.installSignature.clear();
    report.installSignature = installSign(report.signedBytes(), *key);
    wipe(&key->secret);
    const QString problem = report.problem();
    if (!problem.isEmpty()) {
        emit reportSent(false, QStringLiteral("refused"), problem);
        return;
    }
    m_pendingReport = report;
    get(Job::KeysForReport, QStringLiteral("/v1/keys"), kMaxReplyBytes);
}

void ListClient::sendDispute(const Dispute &dispute)
{
    if (busy()) {
        emit disputeSent(false, QStringLiteral("refused"), tr("another request is still running"));
        return;
    }
    const QString problem = dispute.problem();
    if (!problem.isEmpty()) {
        emit disputeSent(false, QStringLiteral("refused"), problem);
        return;
    }
    m_pendingDispute = dispute;
    get(Job::KeysForDispute, QStringLiteral("/v1/keys"), kMaxReplyBytes);
}

void ListClient::get(Job job, const QString &path, qint64 maxBytes)
{
    if (busy()) {
        finish(job, false, QStringLiteral("refused"), tr("another request is still running"));
        return;
    }
    const QString problem = transportProblem();
    if (!problem.isEmpty()) {
        finish(job, false, QStringLiteral("refused"), problem);
        return;
    }
    QNetworkRequest request(url(path));
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    m_job = job;
    m_maxBytes = maxBytes;
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (m_reply && received > m_maxBytes)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, &ListClient::onFinished);
}

void ListClient::post(Job job, const QString &path, const QByteArray &body)
{
    const QString problem = transportProblem();
    if (!problem.isEmpty()) {
        finish(job, false, QStringLiteral("refused"), problem);
        return;
    }
    QNetworkRequest request(url(path));
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    m_job = job;
    m_maxBytes = kMaxReplyBytes;
    m_reply = m_network->post(request, body);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (m_reply && received > m_maxBytes)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, &ListClient::onFinished);
}

void ListClient::onFinished()
{
    QNetworkReply *reply = std::exchange(m_reply, nullptr);
    reply->deleteLater();
    const Job job = std::exchange(m_job, Job::None);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (status == 0 || (reply->error() != QNetworkReply::NoError && status < 400)) {
        finish(job, false, QStringLiteral("refused"), tr("the shared list server could not be reached: %1").arg(reply->errorString()));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);

    if (job == Job::Report || job == Job::Dispute) {
        // The server's answer: {"result": ..., "why": ...}.
        const QString result = document.object().value(QStringLiteral("result")).toString();
        const QString why = document.object().value(QStringLiteral("why")).toString();
        static const QStringList known{QStringLiteral("received"), QStringLiteral("held"), QStringLiteral("refused"),
                                       QStringLiteral("busy")};
        if (!known.contains(result)) {
            finish(job, false, QStringLiteral("refused"), tr("the server's answer is not readable (HTTP %1)").arg(status));
            return;
        }
        finish(job, status == 202, result, why);
        return;
    }
    if (status != 200 || !document.isObject()) {
        finish(job, false, QStringLiteral("refused"), tr("the server answered HTTP %1").arg(status));
        return;
    }
    if (job == Job::KeysForReport || job == Job::KeysForDispute) {
        sealAndPost(document.object());
        return;
    }

    // Job::List
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QString why;
    const qint64 last = m_list ? m_list->list.sequence : 0;
    const std::optional<TrustedList> list = checkBundle(document.object(), m_master, last, now, &why);
    if (!list) {
        finish(job, false, QStringLiteral("refused"), why);
        return;
    }
    QSaveFile file(QDir(m_folder).filePath(kListFile));
    const QByteArray saved = QJsonDocument(QJsonObject{{QStringLiteral("bundle"), document.object()},
                                                       {QStringLiteral("fetchedAt"), timeText(now)}})
                                 .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(saved) != saved.size() || !file.commit()) {
        finish(job, false, QStringLiteral("refused"), tr("the shared list cannot be saved: %1").arg(file.errorString()));
        return;
    }
    m_list = list;
    m_fetchedAt = now;
    finish(job, true, QStringLiteral("received"), QString());
}

void ListClient::sealAndPost(const QJsonObject &keys)
{
    const bool forReport = m_pendingReport.has_value();
    const Job next = forReport ? Job::Report : Job::Dispute;
    QString why;
    const std::optional<Authorization> authorization = checkKeys(keys, m_master, QDateTime::currentDateTimeUtc(), &why);
    if (!authorization) {
        finish(next, false, QStringLiteral("refused"), why);
        return;
    }
    const QByteArray message = forReport ? QJsonDocument(m_pendingReport->toJson()).toJson(QJsonDocument::Compact)
                                         : QJsonDocument(m_pendingDispute->toJson()).toJson(QJsonDocument::Compact);
    const std::optional<QByteArray> envelope
        = seal(message, authorization->serverBox, forReport ? QStringLiteral("report") : QStringLiteral("dispute"));
    if (!envelope) {
        finish(next, false, QStringLiteral("refused"), tr("encrypting failed"));
        return;
    }
    post(next, forReport ? QStringLiteral("/v1/report") : QStringLiteral("/v1/dispute"), *envelope);
}

void ListClient::finish(Job job, bool ok, const QString &result, const QString &why)
{
    switch (job) {
    case Job::List:
        emit listFetched(ok, why);
        return;
    case Job::KeysForReport:
    case Job::Report:
        m_pendingReport.reset();
        emit reportSent(ok, result, why);
        return;
    case Job::KeysForDispute:
    case Job::Dispute:
        m_pendingDispute.reset();
        emit disputeSent(ok, result, why);
        return;
    case Job::None:
        return;
    }
}

std::optional<InstallKey> ListClient::installKey(QString *why)
{
    const QString path = QDir(m_folder).filePath(kInstallFile);
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
            *why = tr("the installation key cannot be read");
            return std::nullopt;
        }
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        const QByteArray::FromBase64Result pub = QByteArray::fromBase64Encoding(
            object.value(QStringLiteral("pub")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        QByteArray::FromBase64Result secret = QByteArray::fromBase64Encoding(
            object.value(QStringLiteral("secret")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!pub || !secret || pub.decoded.size() != 32 || secret.decoded.size() != 64) {
            *why = tr("the installation key is damaged");
            return std::nullopt;
        }
        return InstallKey{pub.decoded, secret.decoded};
    }
    std::optional<InstallKey> key = newInstallKey();
    if (!key) {
        *why = tr("making the installation key failed");
        return std::nullopt;
    }
    QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("pub"), QString::fromLatin1(key->pub.toBase64())},
                                                {QStringLiteral("secret"), QString::fromLatin1(key->secret.toBase64())}})
                          .toJson(QJsonDocument::Compact);
    const bool written = file.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && file.write(json) == json.size() && file.flush();
    wipe(&json);
    if (!written) {
        file.remove();
        *why = tr("the installation key cannot be saved: %1").arg(file.errorString());
        return std::nullopt;
    }
    return key;
}

} // namespace app::sellerlist
