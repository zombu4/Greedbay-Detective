#include "browser/CdpConnection.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>

namespace app::browser {

Q_STATIC_LOGGING_CATEGORY(lcCdp, "browser.cdp")

namespace {

constexpr int kPollIntervalMs = 200;

// A free port on 127.0.0.1, chosen by the operating system, or 0. The port
// is released again at once for the browser to take.
int freeLocalPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}

} // namespace

CdpConnection::CdpConnection(QObject *parent) : QObject(parent) {}

CdpConnection::~CdpConnection()
{
    if (m_socket)
        m_socket->disconnect(this);
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(5000);
    }
}

std::optional<QJsonObject> CdpConnection::parseMessage(const QByteArray &message, QString *error)
{
    if (message.size() > kMaxMessageBytes) {
        *error = QStringLiteral("DevTools message too long");
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("DevTools message is not a JSON object");
        return std::nullopt;
    }
    return document.object();
}

QUrl CdpConnection::webSocketUrlFromVersion(const QByteArray &json, int port)
{
    QString error;
    const std::optional<QJsonObject> object = parseMessage(json, &error);
    if (!object)
        return {};
    const QUrl url(object->value(QStringLiteral("webSocketDebuggerUrl")).toString(), QUrl::StrictMode);
    const QString path = url.path();
    if (!url.isValid() || url.scheme() != QLatin1String("ws") || url.host() != QLatin1String("127.0.0.1")
        || url.port() != port || !path.startsWith(QLatin1String("/devtools/browser/")) || url.hasQuery()
        || url.hasFragment() || !url.userInfo().isEmpty())
        return {};
    for (const QChar c : path) {
        const bool allowed = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                             || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('/') || c == QLatin1Char('-');
        if (!allowed)
            return {};
    }
    return url;
}

bool CdpConnection::start(const QString &program, const QStringList &arguments, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_process)
        return fail(QStringLiteral("the browser is already running"));
    m_port = freeLocalPort();
    if (m_port == 0)
        return fail(QStringLiteral("no free local port for the browser's DevTools connection"));

    m_process = new QProcess(this);
    // The browser's own output is not the app's business and may contain
    // page details; it is discarded (tests may keep its error output).
    m_process->setStandardOutputFile(QProcess::nullDevice());
    m_process->setStandardErrorFile(m_errorFile.isEmpty() ? QProcess::nullDevice() : m_errorFile);
    m_process->setProgram(program);
    m_process->setArguments(arguments + QStringList{QStringLiteral("--remote-debugging-port=%1").arg(m_port)});
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_pollTimer)
            m_pollTimer->stop();
        emit finished(exitCode, status == QProcess::CrashExit);
    });
    m_process->start();
    if (!m_process->waitForStarted(30000)) {
        const QString message = QStringLiteral("cannot start the browser: %1").arg(m_process->errorString());
        delete m_process;
        m_process = nullptr;
        return fail(message);
    }

    m_pollsLeft = kConnectTimeoutMs / kPollIntervalMs;
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &CdpConnection::pollVersion);
    m_network = new QNetworkAccessManager(this);
    m_pollTimer->start();
    return true;
}

void CdpConnection::pollVersion()
{
    if (m_polling)
        return;
    if (--m_pollsLeft <= 0) {
        m_pollTimer->stop();
        emit protocolError(QStringLiteral("the browser did not open its DevTools port in time"));
        return;
    }
    m_polling = true;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/json/version").arg(m_port)));
    request.setTransferTimeout(2000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_polling = false;
        if (!m_pollTimer->isActive() || m_socket)
            return;
        const QUrl url = reply->error() == QNetworkReply::NoError ? webSocketUrlFromVersion(reply->read(65536), m_port) : QUrl();
        if (!url.isEmpty())
            connectWebSocket(url);
    });
}

void CdpConnection::connectWebSocket(const QUrl &url)
{
    m_pollTimer->stop();
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket->setMaxAllowedIncomingMessageSize(static_cast<quint64>(kMaxMessageBytes));
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_connected = true;
        qCInfo(lcCdp) << "connected to the browser on 127.0.0.1 port" << m_port;
        emit connected();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &CdpConnection::onTextMessage);
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        if (m_connected) {
            m_connected = false;
            emit protocolError(QStringLiteral("the DevTools connection closed"));
        }
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_connected)
            emit protocolError(QStringLiteral("cannot connect to the browser: %1").arg(m_socket->errorString()));
    });
    m_socket->open(url);
}

void CdpConnection::onTextMessage(const QString &message)
{
    QString error;
    const std::optional<QJsonObject> object = parseMessage(message.toUtf8(), &error);
    if (!object) {
        emit protocolError(error);
        return;
    }
    const QJsonValue id = object->value(QStringLiteral("id"));
    if (id.isDouble()) {
        emit response(id.toInt(), object->value(QStringLiteral("result")).toObject(),
                      object->value(QStringLiteral("error")).toObject());
        return;
    }
    emit cdpEvent(object->value(QStringLiteral("method")).toString(), object->value(QStringLiteral("params")).toObject(),
               object->value(QStringLiteral("sessionId")).toString());
}

int CdpConnection::send(const QString &method, const QJsonObject &params, const QString &sessionId)
{
    if (!m_connected || !m_socket)
        return -1;
    const int id = m_nextId++;
    QJsonObject message{{QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
    if (!sessionId.isEmpty())
        message.insert(QStringLiteral("sessionId"), sessionId);
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    return id;
}

bool CdpConnection::waitForExit(int timeoutMs)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return true;
    if (m_process->waitForFinished(timeoutMs))
        return true;
    qCWarning(lcCdp) << "the browser did not exit in time; killing it";
    m_process->kill();
    m_process->waitForFinished(5000);
    return false;
}

void CdpConnection::flush()
{
    if (m_socket)
        m_socket->flush();
}

bool CdpConnection::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

qint64 CdpConnection::processId() const
{
    return m_process ? m_process->processId() : 0;
}

} // namespace app::browser
