#include "core/BrowserWorker.h"

#include "core/Version.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace app {

Q_STATIC_LOGGING_CATEGORY(lcClient, "browser.client")

namespace {

QString randomHex()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

} // namespace

BrowserWorker::BrowserWorker(WorkFolder folder, QString program, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_program(std::move(program))
{
}

BrowserWorker::~BrowserWorker()
{
    if (!m_process)
        return;
    // Nobody may receive signals from an object being destroyed.
    blockSignals(true);
    stop();
    if (!waitForFinished(kStopTimeoutMs + 5000) && m_process) {
        m_process->kill();
        m_process->waitForFinished(5000);
    }
}

QString BrowserWorker::defaultProgram()
{
    QString name = QString::fromLatin1(version::kIdentifier) + QStringLiteral("Browser");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
}

bool BrowserWorker::start(QString *error)
{
    if (m_process) {
        // Leaves the running worker and its socket alone.
        if (error)
            *error = QStringLiteral("a browser worker is already running");
        return false;
    }
    auto failStart = [this, error](const QString &message) {
        closeServer();
        if (error)
            *error = message;
        return false;
    };

    m_token = randomHex();
    m_reader = browserprotocol::LineReader();
    m_nextId = 1;
    m_pendingId = -1;
    m_quitId = -1;
    m_ready = false;
    m_stopping = false;
    m_failed = false;

#ifdef Q_OS_WIN
    // A named pipe; UserAccessOption restricts it to the current user.
    const QString name = QString::fromLatin1(version::kIdentifier) + QStringLiteral("-browser-") + randomHex();
#else
    // A socket file in the owner-only work folder. A Unix socket path must
    // fit in sockaddr_un (104 bytes on macOS, 108 on Linux, with the final
    // zero byte); a longer work folder path cannot be used for the socket.
    const QString name = m_folder.tmpDir() + QStringLiteral("/bw-") + randomHex().left(8);
    if (QFile::encodeName(name).size() > kMaxSocketPathBytes)
        return failStart(QStringLiteral("the work folder path is too long for the browser worker socket "
                                        "(at most %1 bytes for <work folder>/tmp/bw-xxxxxxxx)")
                             .arg(kMaxSocketPathBytes));
#endif
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->setMaxPendingConnections(1);
    QLocalServer::removeServer(name);
    if (!m_server->listen(name))
        return failStart(QStringLiteral("cannot open the browser worker socket: %1").arg(m_server->errorString()));
    connect(m_server, &QLocalServer::newConnection, this, &BrowserWorker::onNewConnection);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);

    m_process = new QProcess(this);
    // The worker logs to its own file; its stderr is passed through so that
    // messages from Qt WebEngine before logging starts are not lost.
    m_process->setProcessChannelMode(QProcess::ForwardedChannels);
    m_process->setProgram(m_program);
    m_process->setArguments({QStringLiteral("--work-folder"), m_folder.path(), QStringLiteral("--server"),
                             m_server->fullServerName(), QStringLiteral("--token"), m_token});
    connect(m_process, &QProcess::finished, this, &BrowserWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart) {
            const QString message =
                QStringLiteral("cannot start the browser worker: %1").arg(m_process->errorString());
            fail(message);
            // A process that never started emits no finished(); end the
            // session here.
            onProcessFinished(-1, QProcess::CrashExit);
        }
    });

    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (m_stopping) {
            qCWarning(lcClient) << "browser worker did not exit in time; killing it";
            if (m_process)
                m_process->kill();
        } else if (!m_ready) {
            fail(QStringLiteral("the browser worker did not start in time"));
        }
    });
    m_timer->start(kStartTimeoutMs);

    m_process->start();
    // On some systems a program that cannot be started is reported inside
    // start(); the session has then already ended.
    if (!m_process) {
        if (error)
            *error = m_lastError;
        return false;
    }
    qCInfo(lcClient) << "browser worker starting:" << QDir::toNativeSeparators(m_program);
    return true;
}

int BrowserWorker::request(QJsonObject command)
{
    if (!m_ready || m_pendingId >= 0 || m_stopping || !m_socket)
        return -1;
    const int id = m_nextId++;
    command.insert(QStringLiteral("id"), id);
    m_pendingId = id;
    m_socket->write(browserprotocol::encode(command));
    return id;
}

void BrowserWorker::stop()
{
    if (!m_process || m_stopping)
        return;
    m_stopping = true;
    if (m_ready && m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        const int id = m_nextId++;
        m_quitId = id;
        QJsonObject quit{{QStringLiteral("id"), id},
                         {QStringLiteral("cmd"), QString::fromLatin1(browserprotocol::kCmdQuit)}};
        m_socket->write(browserprotocol::encode(quit));
        m_socket->flush();
        m_timer->start(kStopTimeoutMs);
    } else {
        // Not connected yet, so no browser session exists in the worker.
        m_process->kill();
    }
}

bool BrowserWorker::waitForFinished(int timeoutMs)
{
    if (!m_process)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    // finished() may be blocked by the destructor; watch the process too.
    connect(m_process, &QProcess::finished, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
    return !m_process;
}

bool BrowserWorker::isRunning() const
{
    return m_process != nullptr;
}

qint64 BrowserWorker::processId() const
{
    return m_process ? m_process->processId() : 0;
}

void BrowserWorker::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        if (m_socket) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        // The server owns accepted sockets and is deleted below; the socket
        // must outlive it.
        m_socket->setParent(this);
        connect(m_socket, &QLocalSocket::readyRead, this, &BrowserWorker::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, [this]() {
            if (!m_stopping)
                fail(QStringLiteral("the connection to the browser worker closed"));
        });
    }
    // One connection per worker: stop listening (removes the socket file).
    closeServer();
}

void BrowserWorker::onReadyRead()
{
    if (!m_socket)
        return;
    m_reader.append(m_socket->readAll());
    QString error;
    while (true) {
        const std::optional<QJsonObject> message = m_reader.next(&error);
        if (!message) {
            if (!error.isEmpty())
                fail(error);
            return;
        }
        if (!m_ready) {
            if (message->value(QStringLiteral("event")).toString() != QLatin1String(browserprotocol::kEventReady)
                || message->value(QStringLiteral("token")).toString() != m_token) {
                fail(QStringLiteral("the browser worker sent an unexpected first message"));
                return;
            }
            m_ready = true;
            if (!m_stopping)
                m_timer->stop();
            qCInfo(lcClient) << "browser worker ready, pid" << processId();
            emit ready();
            continue;
        }
        const int id = message->value(QStringLiteral("id")).toInt(-1);
        if (id >= 0 && id == m_pendingId) {
            m_pendingId = -1;
        } else if (id >= 0 && id == m_quitId) {
            m_quitId = -1;
        } else {
            fail(QStringLiteral("the browser worker sent a reply nobody asked for"));
            return;
        }
        emit replied(id, *message);
    }
}

void BrowserWorker::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    // Replies the worker wrote just before exiting may still be unread: the
    // exit can be reported before the socket's last data.
    if (m_socket && !m_failed) {
        m_socket->waitForReadyRead(0);
        onReadyRead();
    }
    const bool normal = status == QProcess::NormalExit && exitCode == 0 && m_stopping && !m_failed;
    if (!m_stopping && !m_failed)
        fail(QStringLiteral("the browser worker exited unexpectedly (exit code %1)").arg(exitCode));
    qCInfo(lcClient) << "browser worker exited, code" << exitCode << (normal ? "(normal)" : "(abnormal)");

    // A request still open gets its one reply.
    const int pendingId = m_pendingId;
    m_pendingId = -1;
    if (pendingId >= 0)
        emit replied(pendingId, QJsonObject{{QStringLiteral("status"), QString::fromLatin1(browserprotocol::kStatusFailed)},
                                            {QStringLiteral("error"), QStringLiteral("the browser worker ended")}});

    closeServer();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    m_ready = false;
    m_pendingId = -1;
    m_quitId = -1;
    emit finished(normal, exitCode);
}

void BrowserWorker::fail(const QString &error)
{
    if (m_failed)
        return;
    m_failed = true;
    m_lastError = error;
    qCWarning(lcClient) << "browser worker:" << error;
    emit failed(error);
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->kill();
    }
}

void BrowserWorker::closeServer()
{
    if (!m_server)
        return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

} // namespace app
