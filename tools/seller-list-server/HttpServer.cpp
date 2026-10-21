#include "HttpServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace sellerserver {

namespace {

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
    : QObject(parent), m_handler(std::move(handler)), m_limits(limits), m_server(new QTcpServer(this))
{
    m_server->setMaxPendingConnections(m_limits.maxConnections);
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::HttpServer(Handler handler, QObject *parent) : HttpServer(std::move(handler), Limits{}, parent) {}

HttpServer::~HttpServer() = default;

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
            socket->abort();
            socket->deleteLater();
            continue;
        }
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
