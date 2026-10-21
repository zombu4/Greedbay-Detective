#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace sellerserver {

// A deliberately small HTTP/1.1 server for the onion service: tor forwards
// the onion's port to this loopback port, so nothing but tor reaches it.
// One request per connection (the reply closes it), a body only with
// Content-Length (no chunked transfer), hard limits on header and body size,
// on open connections and on the time a connection may take. Nothing about
// a client is logged (tor hides who it is anyway).
class HttpServer : public QObject {
    Q_OBJECT
public:
    struct Request {
        QByteArray method;
        QByteArray path; // without the query
        QByteArray body;
    };
    struct Response {
        int status = 200;
        QByteArray contentType = "application/json";
        QByteArray body;
    };
    using Handler = std::function<Response(const Request &)>;

    struct Limits {
        qsizetype maxHeaderBytes = 8 * 1024;
        qsizetype maxBodyBytes = 16 * 1024;
        int maxConnections = 32;
        int connectionMs = 30 * 1000;
    };

    explicit HttpServer(Handler handler, Limits limits, QObject *parent = nullptr);
    explicit HttpServer(Handler handler, QObject *parent = nullptr);
    ~HttpServer() override;

    // Listens on `address`:`port` (0 = any free port); false and *error when
    // it cannot.
    bool listen(const QHostAddress &address, quint16 port, QString *error);
    quint16 port() const;

    static QByteArray reasonPhrase(int status);

private:
    struct Connection {
        QByteArray buffer;
        QTimer *timer = nullptr;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void reply(QTcpSocket *socket, const Response &response);
    void drop(QTcpSocket *socket);

    Handler m_handler;
    Limits m_limits;
    QTcpServer *m_server;
    QHash<QTcpSocket *, Connection> m_connections;
};

} // namespace sellerserver
