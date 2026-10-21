// A stand-in for tor in tests of TorRunner: takes tor's command-line options,
// writes the cookie file and the ControlPortWriteToFile line, and answers the
// control commands TorRunner sends the way the tor control-spec describes
// (PROTOCOLINFO, AUTHCHALLENGE SAFECOOKIE, AUTHENTICATE, TAKEOWNERSHIP,
// GETINFO net/listeners/socks and status/bootstrap-phase). It exits when the
// control connection closes (TAKEOWNERSHIP). A file "wrong-hash" next to the
// data folder makes it answer the challenge with a wrong server hash.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

QByteArray randomBytes(int size)
{
    QByteArray bytes(size, '\0');
    for (char &byte : bytes)
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return bytes;
}

QByteArray hmac(const QByteArray &key, const QByteArray &message)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    auto option = [&](const QString &name) {
        const qsizetype at = args.indexOf(name);
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
    };
    const QString dataDir = option(QStringLiteral("--DataDirectory"));
    const QString cookiePath = option(QStringLiteral("--CookieAuthFile"));
    const QString portFile = option(QStringLiteral("--ControlPortWriteToFile"));
    if (dataDir.isEmpty() || cookiePath.isEmpty() || portFile.isEmpty())
        return 2;
    const bool wrongHash = QFileInfo::exists(QFileInfo(dataDir).dir().filePath(QStringLiteral("wrong-hash")));

    const QByteArray cookie = randomBytes(32);
    QFile cookieFile(cookiePath);
    if (!cookieFile.open(QIODevice::WriteOnly) || cookieFile.write(cookie) != 32)
        return 3;
    cookieFile.close();

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 4;
    QFile port(portFile);
    if (!port.open(QIODevice::WriteOnly))
        return 5;
    port.write("PORT=127.0.0.1:" + QByteArray::number(server.serverPort()) + "\n");
    port.close();

    QByteArray clientNonce;
    QByteArray serverNonce;
    int bootstrapAsks = 0;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, &application, &QCoreApplication::quit);
        QObject::connect(socket, &QTcpSocket::readyRead, [&, socket, buffer] {
            buffer->append(socket->readAll());
            qsizetype end;
            while ((end = buffer->indexOf("\r\n")) >= 0) {
                const QByteArray line = buffer->left(end);
                buffer->remove(0, end + 2);
                if (line == "PROTOCOLINFO 1") {
                    socket->write("250-PROTOCOLINFO 1\r\n250-AUTH METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\""
                                  + cookiePath.toUtf8() + "\"\r\n250-VERSION Tor=\"0.4.9.14\"\r\n250 OK\r\n");
                } else if (line.startsWith("AUTHCHALLENGE SAFECOOKIE ")) {
                    clientNonce = QByteArray::fromHex(line.mid(25));
                    serverNonce = randomBytes(32);
                    const QByteArray serverHash = wrongHash ? randomBytes(32)
                                                            : hmac("Tor safe cookie authentication server-to-controller hash",
                                                                   cookie + clientNonce + serverNonce);
                    socket->write("250 AUTHCHALLENGE SERVERHASH=" + serverHash.toHex().toUpper() + " SERVERNONCE="
                                  + serverNonce.toHex().toUpper() + "\r\n");
                } else if (line.startsWith("AUTHENTICATE ")) {
                    const QByteArray expected
                        = hmac("Tor safe cookie authentication controller-to-server hash", cookie + clientNonce + serverNonce);
                    if (QByteArray::fromHex(line.mid(13)) != expected) {
                        socket->write("515 Bad authentication\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    socket->write("250 OK\r\n");
                } else if (line == "TAKEOWNERSHIP") {
                    socket->write("250 OK\r\n");
                } else if (line == "GETINFO net/listeners/socks") {
                    socket->write("250-net/listeners/socks=\"127.0.0.1:19050\"\r\n250 OK\r\n");
                } else if (line == "GETINFO status/bootstrap-phase") {
                    ++bootstrapAsks;
                    socket->write(bootstrapAsks < 2
                                      ? "250-status/bootstrap-phase=NOTICE BOOTSTRAP PROGRESS=50 TAG=loading_descriptors SUMMARY=\"Loading\"\r\n250 OK\r\n"
                                      : "250-status/bootstrap-phase=NOTICE BOOTSTRAP PROGRESS=100 TAG=done SUMMARY=\"Done\"\r\n250 OK\r\n");
                } else {
                    socket->write("510 Unrecognized command\r\n");
                }
            }
        });
    });
    return QCoreApplication::exec();
}
