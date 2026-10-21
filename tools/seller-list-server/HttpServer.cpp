#include "HttpServer.h"

#include <QFile>
#include <QFileInfo>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>
#include <QTimer>

#include <optional>

namespace sellerserver {

namespace {

// The TLS settings from the two PEM files; std::nullopt and *error when
// they do not hold a certificate and its matching private key.
std::optional<QSslConfiguration> tlsConfiguration(const HttpServer::Tls &tls, QString *error)
{
    const QList<QSslCertificate> chain = QSslCertificate::fromPath(tls.certificateFile, QSsl::Pem, QSslCertificate::PatternSyntax::FixedString);
    if (chain.isEmpty()) {
        *error = QStringLiteral("%1 holds no PEM certificate").arg(tls.certificateFile);
        return std::nullopt;
    }
    QFile keyFile(tls.keyFile);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 cannot be read: %2").arg(tls.keyFile, keyFile.errorString());
        return std::nullopt;
    }
    // The key is read as the kind the certificate's public key is (RSA or
    // EC; certbot makes EC keys by default since its version 2.0).
    const QSsl::KeyAlgorithm algorithm = chain.first().publicKey().algorithm();
    if (algorithm != QSsl::Rsa && algorithm != QSsl::Ec) {
        *error = QStringLiteral("the certificate in %1 has neither an RSA nor an EC key").arg(tls.certificateFile);
        return std::nullopt;
    }
    const QSslKey key(keyFile.readAll(), algorithm, QSsl::Pem, QSsl::PrivateKey);
    if (key.isNull()) {
        *error = QStringLiteral("%1 holds no PEM private key of the certificate's kind").arg(tls.keyFile);
        return std::nullopt;
    }
    if (chain.first().expiryDate() <= QDateTime::currentDateTimeUtc()) {
        *error = QStringLiteral("the certificate in %1 expired %2").arg(tls.certificateFile, chain.first().expiryDate().toString(Qt::ISODate));
        return std::nullopt;
    }
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificateChain(chain);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    return configuration;
}

QDateTime modified(const QString &path) { return QFileInfo(path).lastModified(QTimeZone::UTC); }

// The value of header `name` (case-insensitive) in the header block, or a
// null array.
QByteArray headerValue(const QList<QByteArray> &lines, const QByteArray &name)
{
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines.at(i).indexOf(':');
        if (colon > 0 && lines.at(i).left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return lines.at(i).mid(colon + 1).trimmed();
    }
    return {};
}

} // namespace

HttpServer::HttpServer(Handler handler, Limits limits, QObject *parent)
    : QObject(parent), m_handler(std::move(handler)), m_limits(limits)
{
    useServer(new QTcpServer(this));
}

HttpServer::HttpServer(Handler handler, QObject *parent) : HttpServer(std::move(handler), Limits{}, parent) {}

HttpServer::~HttpServer() = default;

void HttpServer::useServer(QTcpServer *server)
{
    delete m_server;
    m_server = server;
    m_server->setMaxPendingConnections(m_limits.maxConnections);
    // pendingConnectionAvailable: a plain connection at once, a TLS one once
    // its handshake is done (QSslServer).
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this, &HttpServer::onNewConnection);
}

bool HttpServer::setTls(const Tls &tls, QString *error)
{
    const std::optional<QSslConfiguration> configuration = tlsConfiguration(tls, error);
    if (!configuration)
        return false;
    auto *server = new QSslServer(this);
    server->setSslConfiguration(*configuration);
    server->setHandshakeTimeout(m_limits.handshakeMs);
    useServer(server);
    m_tls = tls;
    m_certificateTime = modified(tls.certificateFile);
    m_keyTime = modified(tls.keyFile);
    if (!m_tlsCheck) {
        m_tlsCheck = new QTimer(this);
        connect(m_tlsCheck, &QTimer::timeout, this, &HttpServer::reloadTlsIfChanged);
    }
    m_tlsCheck->start(m_limits.tlsCheckMs);
    return true;
}

void HttpServer::reloadTlsIfChanged()
{
    // Renewal tools replace both files; a pair is read once neither has
    // changed for a whole check interval, so a half-copied pair is not used.
    const QDateTime certificateTime = modified(m_tls.certificateFile);
    const QDateTime keyTime = modified(m_tls.keyFile);
    if (certificateTime == m_certificateTime && keyTime == m_keyTime) {
        m_pendingCertificateTime = {};
        m_pendingKeyTime = {};
        return;
    }
    if (certificateTime != m_pendingCertificateTime || keyTime != m_pendingKeyTime) {
        m_pendingCertificateTime = certificateTime;
        m_pendingKeyTime = keyTime;
        return;
    }
    m_certificateTime = certificateTime;
    m_keyTime = keyTime;
    m_pendingCertificateTime = {};
    m_pendingKeyTime = {};
    QString why;
    const std::optional<QSslConfiguration> configuration = tlsConfiguration(m_tls, &why);
    if (!configuration) {
        emit tlsReloadFailed(why);
        return;
    }
    // New connections use it; open ones finish with the old one.
    static_cast<QSslServer *>(m_server)->setSslConfiguration(*configuration);
    emit tlsReloaded();
}

HttpServer::Counts HttpServer::takeCounts() { return std::exchange(m_counts, Counts{}); }

bool HttpServer::listen(const QHostAddress &address, quint16 port, QString *error)
{
    if (m_server->listen(address, port))
        return true;
    *error = m_server->errorString();
    return false;
}

quint16 HttpServer::port() const { return m_server->serverPort(); }

QByteArray HttpServer::reasonPhrase(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 202:
        return "Accepted";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Content Too Large";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "Status";
    }
}

void HttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        if (m_connections.size() >= m_limits.maxConnections) {
            ++m_counts.turnedAway;
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++m_counts.connections;
        Connection connection;
        connection.timer = new QTimer(socket);
        connection.timer->setSingleShot(true);
        connect(connection.timer, &QTimer::timeout, this, [this, socket] {
            auto it = m_connections.find(socket);
            if (it == m_connections.end())
                return;
            // A client that does not read the reply is cut off as well.
            if (it->replied)
                socket->abort();
            else
                reply(socket, {408, "text/plain", "timeout\n"});
        });
        connection.timer->start(m_limits.connectionMs);
        m_connections.insert(socket, connection);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { drop(socket); });
        if (socket->bytesAvailable() > 0)
            onReadyRead(socket);
    }
}

void HttpServer::onReadyRead(QTcpSocket *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    QByteArray &buffer = it->buffer;
    buffer.append(socket->readAll());

    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > m_limits.maxHeaderBytes)
            reply(socket, {431, "text/plain", "header too large\n"});
        return;
    }
    if (headerEnd > m_limits.maxHeaderBytes) {
        reply(socket, {431, "text/plain", "header too large\n"});
        return;
    }
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(2).startsWith("HTTP/1.")) {
        reply(socket, {400, "text/plain", "bad request\n"});
        return;
    }
    if (!headerValue(lines, "Transfer-Encoding").isNull()) {
        reply(socket, {411, "text/plain", "Content-Length required\n"});
        return;
    }
    qsizetype length = 0;
    const QByteArray lengthText = headerValue(lines, "Content-Length");
    if (!lengthText.isNull()) {
        bool ok = false;
        length = lengthText.toLongLong(&ok);
        if (!ok || length < 0) {
            reply(socket, {400, "text/plain", "bad Content-Length\n"});
            return;
        }
    }
    if (length > m_limits.maxBodyBytes) {
        reply(socket, {413, "text/plain", "body too large\n"});
        return;
    }
    const qsizetype bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < length)
        return; // the rest of the body is still coming
    if (buffer.size() - bodyStart > length) {
        reply(socket, {400, "text/plain", "one request per connection\n"});
        return;
    }

    Request request;
    request.method = requestLine.at(0);
    const QByteArray target = requestLine.at(1);
    const qsizetype query = target.indexOf('?');
    request.path = query < 0 ? target : target.left(query);
    request.body = buffer.mid(bodyStart, length);
    reply(socket, m_handler(request));
}

void HttpServer::reply(QTcpSocket *socket, const Response &response)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    if (it->replied)
        return;
    it->replied = true;
    ++m_counts.replies[response.status];
    it->timer->start(m_limits.connectionMs);
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reasonPhrase(response.status) + "\r\n";
    head += "Content-Type: " + response.contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    socket->write(head);
    socket->write(response.body);
    socket->disconnectFromHost();
}

void HttpServer::drop(QTcpSocket *socket)
{
    m_connections.remove(socket);
    socket->deleteLater();
}

} // namespace sellerserver
