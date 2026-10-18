#include "browser/WorkerHost.h"

#include "ebay/PageCheck.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUrl>

#include <utility>

namespace app::browser {

Q_STATIC_LOGGING_CATEGORY(lcWorker, "browser.worker")

namespace protocol = browserprotocol;

WorkerHost::WorkerHost(WorkFolder folder, SessionOptions options, QLockFile *sessionLock, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_options(options), m_sessionLock(sessionLock)
{
}

WorkerHost::~WorkerHost()
{
    if (m_socket)
        m_socket->disconnect(this);
    m_session.reset();
}

bool WorkerHost::connectToApp(const QString &serverName, const QString &token, QString *error)
{
    m_socket = new QLocalSocket(this);
    m_socket->connectToServer(serverName);
    if (!m_socket->waitForConnected(30000)) {
        if (error)
            *error = QStringLiteral("cannot connect to the app: %1").arg(m_socket->errorString());
        return false;
    }
    connect(m_socket, &QLocalSocket::readyRead, this, &WorkerHost::onReadyRead);
    // Queued: a write that finds the app gone can report the disconnection
    // inside the write, while the session is still sending its reply.
    connect(m_socket, &QLocalSocket::disconnected, this, &WorkerHost::onDisconnected, Qt::QueuedConnection);
    send(QJsonObject{{QStringLiteral("event"), QString::fromLatin1(protocol::kEventReady)},
                     {QStringLiteral("token"), token}});
    qCInfo(lcWorker) << "connected to the app";
    return true;
}

void WorkerHost::onReadyRead()
{
    m_reader.append(m_socket->readAll());
    QString error;
    while (!m_quitting) {
        const std::optional<QJsonObject> message = m_reader.next(&error);
        if (!message) {
            if (!error.isEmpty()) {
                qCWarning(lcWorker).noquote() << "protocol error:" << error;
                m_socket->abort();
            }
            return;
        }
        handle(*message);
    }
}

void WorkerHost::handle(const QJsonObject &message)
{
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    const QString command = message.value(QStringLiteral("cmd")).toString();
    if (id < 0) {
        qCWarning(lcWorker) << "request without id; closing the connection";
        m_socket->abort();
        return;
    }
    if (command == QLatin1String(protocol::kCmdQuit)) {
        quit(id);
        return;
    }
    if (m_currentId >= 0) {
        reply(id, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(protocol::kStatusFailed)},
                              {QStringLiteral("error"), QStringLiteral("another request is still running")}});
        return;
    }
    if (command == QLatin1String(protocol::kCmdFetch)) {
        const std::optional<ebay::PageKind> kind =
            ebay::pageKindFromName(message.value(QStringLiteral("kind")).toString());
        const QUrl url(message.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        if (!kind || !url.isValid()) {
            reply(id, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(protocol::kStatusFailed)},
                                  {QStringLiteral("error"), QStringLiteral("fetch needs a known kind and a valid url")}});
            return;
        }
        if (!ensureSession(id))
            return;
        m_currentId = id;
        m_session->fetch(url, *kind);
        return;
    }
    if (command == QLatin1String(protocol::kCmdSignIn)) {
        if (!ensureSession(id))
            return;
        m_currentId = id;
        m_session->signIn();
        return;
    }
    reply(id, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(protocol::kStatusFailed)},
                          {QStringLiteral("error"), QStringLiteral("unknown command")}});
}

bool WorkerHost::ensureSession(int id)
{
    if (m_session)
        return true;
    QString error;
    m_session = BrowserSession::create(m_folder, m_options, m_sessionLock, &error);
    if (!m_session) {
        qCWarning(lcWorker).noquote() << "cannot start the browser session:" << error;
        reply(id, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(protocol::kStatusFailed)},
                              {QStringLiteral("error"), error}});
        return false;
    }
    connect(m_session.get(), &BrowserSession::done, this, [this](const QJsonObject &result) {
        const int answered = m_currentId;
        m_currentId = -1;
        if (answered >= 0)
            reply(answered, result);
    });
    return true;
}

void WorkerHost::send(const QJsonObject &message)
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write(protocol::encode(message));
        m_socket->flush();
    }
}

void WorkerHost::reply(int id, QJsonObject message)
{
    message.insert(QStringLiteral("id"), id);
    send(message);
}

void WorkerHost::quit(int id)
{
    m_quitting = true;
    // A running request is answered first ("failed: stopped").
    if (m_session)
        m_session->cancel();
    m_session.reset();
    qCInfo(lcWorker) << "browser torn down; exiting";
    reply(id, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(protocol::kStatusOk)}});
    if (m_socket) {
        m_socket->waitForBytesWritten(5000);
        m_socket->disconnect(this);
        m_socket->disconnectFromServer();
    }
    QCoreApplication::exit(0);
}

void WorkerHost::onDisconnected()
{
    if (m_quitting)
        return;
    m_quitting = true;
    qCWarning(lcWorker) << "connection to the app lost; tearing the browser down";
    m_session.reset();
    QCoreApplication::exit(kExitConnectionLost);
}

} // namespace app::browser
