#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace sellerserver {

// A deliberately small HTTP/1.1 server. With setTls() it speaks HTTPS itself
// (the Let's Encrypt files, port 443 open to the internet); without it, it
// serves plain HTTP for a reverse proxy in front of it on the same machine.
// One request per connection (the reply closes it), a body only with
// Content-Length (no chunked transfer), hard limits on header and body size,
// on open connections and on the time a connection may take (the TLS
// handshake, reading the request, and again for the reply).
// Nothing about a client is logged or kept: no address, no headers, no
// path. The only record is takeCounts(): how many connections and replies
// of each status, for the owner's hourly log line.
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
        int handshakeMs = 10 * 1000; // TLS only
        int tlsCheckMs = 60 * 1000;  // how often setTls()'s files are looked at
    };

    struct Tls {
        QString certificateFile; // PEM: the server's certificate, then the chain (Let's Encrypt fullchain.pem)
        QString keyFile;         // PEM: its private key (privkey.pem), RSA or EC
    };

    struct Counts {
        int connections = 0; // accepted (after the TLS handshake)
        int turnedAway = 0;  // closed at once: all connection slots in use
        QMap<int, int> replies; // HTTP status -> how many
    };

    explicit HttpServer(Handler handler, Limits limits, QObject *parent = nullptr);
    explicit HttpServer(Handler handler, QObject *parent = nullptr);
    ~HttpServer() override;

    // Before listen(): serve HTTPS with these files (TLS 1.2 or newer, no
    // client certificates). They are read now (false and *error when they do
    // not load) and again whenever either file's time changes (checked every
    // Limits::tlsCheckMs), so a renewed certificate is used without a restart; a renewed
    // pair that does not load is reported by tlsReloadFailed() and the last
    // good pair stays in use.
    bool setTls(const Tls &tls, QString *error);

    // Listens on `address`:`port` (0 = any free port); false and *error when
    // it cannot.
    bool listen(const QHostAddress &address, quint16 port, QString *error);
    quint16 port() const;

    static QByteArray reasonPhrase(int status);

    // Replaces the request handler (the server may listen before the service
    // behind it is ready).
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    // The counts since the last call (and starts new ones).
    Counts takeCounts();

signals:
    void tlsReloaded();
    void tlsReloadFailed(const QString &why);

private:
    struct Connection {
        QByteArray buffer;
        QTimer *timer = nullptr;
        bool replied = false;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void reply(QTcpSocket *socket, const Response &response);
    void drop(QTcpSocket *socket);
    void useServer(QTcpServer *server);
    void reloadTlsIfChanged();

    Handler m_handler;
    Limits m_limits;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Connection> m_connections;
    Counts m_counts;
    Tls m_tls;
    QDateTime m_certificateTime;
    QDateTime m_keyTime;
    QDateTime m_pendingCertificateTime;
    QDateTime m_pendingKeyTime;
    QTimer *m_tlsCheck = nullptr;
};

} // namespace sellerserver
