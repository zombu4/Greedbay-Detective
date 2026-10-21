#include "sellerlist/TorRunner.h"

#include "sellerlist/Crypto.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

#include <sodium.h>

namespace app::sellerlist {

namespace {

constexpr int kControlPollMs = 200;
constexpr int kBootstrapPollMs = 1000;
constexpr int kMaxControlReply = 64 * 1024;
constexpr int kCookieBytes = 32;
constexpr int kNonceBytes = 32;
constexpr int kKeptOutputLines = 20;

const QByteArray kServerHashKey = "Tor safe cookie authentication server-to-controller hash";
const QByteArray kControllerHashKey = "Tor safe cookie authentication controller-to-server hash";

QByteArray hmac(const QByteArray &key, const QByteArray &message)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

// KEY=VALUE from a control reply line ("250 AUTHCHALLENGE SERVERHASH=..").
QByteArray field(const QByteArray &line, const QByteArray &key)
{
    const QList<QByteArray> parts = line.split(' ');
    for (const QByteArray &part : parts) {
        if (part.startsWith(key + '='))
            return part.mid(key.size() + 1);
    }
    return {};
}

} // namespace

TorRunner::TorRunner(QString program, QString torDir, QObject *parent)
    : QObject(parent),
      m_program(std::move(program)),
      m_dir(std::move(torDir)),
      m_poll(new QTimer(this)),
      m_timeout(new QTimer(this)),
      m_bootstrap(new QTimer(this))
{
    m_poll->setInterval(kControlPollMs);
    connect(m_poll, &QTimer::timeout, this, &TorRunner::pollControlFile);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &TorRunner::onStartTimeout);
    m_bootstrap->setInterval(kBootstrapPollMs);
    connect(m_bootstrap, &QTimer::timeout, this, [this] {
        if (m_step == Step::Bootstrap)
            send("GETINFO status/bootstrap-phase");
    });
}

TorRunner::~TorRunner() { cleanUp(); }

QString TorRunner::bundledProgram()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("tor/tor.exe");
#else
    const QString name = QStringLiteral("tor/tor");
#endif
    const QFileInfo info(QDir(QCoreApplication::applicationDirPath()).filePath(name));
    return info.isFile() && info.isExecutable() ? info.filePath() : QString();
}

QNetworkProxy TorRunner::proxy() const
{
    return QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), m_socksPort);
}

void TorRunner::start()
{
    if (m_state == State::Starting || m_state == State::Ready)
        return;
    cleanUp();
    m_error.clear();
    m_progress = tr("starting Tor");
    if (m_program.isEmpty() || !QFileInfo(m_program).isExecutable()) {
        fail(tr("Tor is not part of this build of the app"));
        return;
    }
    const QDir dir(m_dir);
    if (!dir.mkpath(QStringLiteral("data"))) {
        fail(tr("cannot create %1").arg(dir.filePath(QStringLiteral("data"))));
        return;
    }
    // tor refuses a data folder others can read.
    QFile::setPermissions(dir.filePath(QStringLiteral("data")), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                                    | QFileDevice::ExeOwner);
    QFile::remove(dir.filePath(QStringLiteral("control-port")));
    for (const QString &empty : {QStringLiteral("torrc"), QStringLiteral("torrc-defaults")}) {
        QFile file(dir.filePath(empty));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fail(tr("cannot write %1").arg(file.fileName()));
            return;
        }
    }

    m_process = new QProcess(this);
    m_process->setProgram(m_program);
    m_process->setWorkingDirectory(m_dir);
    m_process->setArguments({QStringLiteral("-f"), dir.filePath(QStringLiteral("torrc")),
                             QStringLiteral("--defaults-torrc"), dir.filePath(QStringLiteral("torrc-defaults")),
                             QStringLiteral("--DataDirectory"), dir.filePath(QStringLiteral("data")),
                             QStringLiteral("--SocksPort"), QStringLiteral("127.0.0.1:auto"),
                             QStringLiteral("--ControlPort"), QStringLiteral("127.0.0.1:auto"),
                             QStringLiteral("--ControlPortWriteToFile"), dir.filePath(QStringLiteral("control-port")),
                             QStringLiteral("--CookieAuthentication"), QStringLiteral("1"),
                             QStringLiteral("--CookieAuthFile"), dir.filePath(QStringLiteral("data/control_auth_cookie")),
                             QStringLiteral("--__OwningControllerProcess"), QString::number(QCoreApplication::applicationPid()),
                             QStringLiteral("--AvoidDiskWrites"), QStringLiteral("1"),
                             QStringLiteral("--ClientOnly"), QStringLiteral("1"),
                             QStringLiteral("--Log"), QStringLiteral("notice stdout")});
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        while (m_process && m_process->canReadLine()) {
            m_lastOutput.append(QString::fromUtf8(m_process->readLine()).trimmed());
            if (m_lastOutput.size() > kKeptOutputLines)
                m_lastOutput.removeFirst();
        }
    });
    connect(m_process, &QProcess::finished, this, &TorRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Tor could not be started: %1").arg(m_process->errorString()));
    });
    setState(State::Starting);
    m_timeout->start(kStartTimeoutMs);
    m_process->start();
    // A start that failed at once has already ended in fail().
    if (m_process && m_state == State::Starting)
        m_poll->start();
}

void TorRunner::stop()
{
    cleanUp();
    m_progress.clear();
    setState(State::Off);
}

void TorRunner::cleanUp()
{
    m_poll->stop();
    m_timeout->stop();
    m_bootstrap->stop();
    m_step = Step::None;
    m_buffer.clear();
    m_reply.clear();
    sodium_memzero(m_cookie.data(), static_cast<size_t>(m_cookie.size()));
    m_cookie.clear();
    m_socksPort = 0;
    if (m_control) {
        // TAKEOWNERSHIP: closing this connection makes tor exit.
        disconnect(m_control, nullptr, this, nullptr);
        m_control->abort();
        m_control->deleteLater();
        m_control = nullptr;
    }
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(5000))
                m_process->kill();
            m_process->waitForFinished(2000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void TorRunner::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    const QString last = m_lastOutput.isEmpty() ? QString() : m_lastOutput.last();
    fail(status == QProcess::CrashExit ? tr("Tor stopped unexpectedly")
                                       : tr("Tor exited (code %1)%2").arg(exitCode).arg(last.isEmpty() ? QString() : QStringLiteral(": ") + last));
}

void TorRunner::onStartTimeout()
{
    fail(tr("Tor could not connect within %1 seconds (%2)").arg(kStartTimeoutMs / 1000).arg(m_progress));
}

void TorRunner::pollControlFile()
{
    QFile file(QDir(m_dir).filePath(QStringLiteral("control-port")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    // One line per control listener: "PORT=127.0.0.1:<port>".
    const QByteArray line = file.readLine(256).trimmed();
    static const QRegularExpression pattern(QStringLiteral("^PORT=127\\.0\\.0\\.1:(\\d{1,5})$"));
    const QRegularExpressionMatch match = pattern.match(QString::fromLatin1(line));
    if (!match.hasMatch())
        return; // not written completely yet
    const uint port = match.captured(1).toUInt();
    if (port == 0 || port > 65535)
        return;
    m_poll->stop();
    m_control = new QTcpSocket(this);
    connect(m_control, &QTcpSocket::readyRead, this, &TorRunner::onControlReadyRead);
    connect(m_control, &QTcpSocket::errorOccurred, this,
            [this] { fail(tr("Tor's control connection failed: %1").arg(m_control->errorString())); });
    connect(m_control, &QTcpSocket::connected, this, [this] {
        m_progress = tr("authenticating to Tor");
        m_step = Step::ProtocolInfo;
        send("PROTOCOLINFO 1");
    });
    m_control->connectToHost(QHostAddress::LocalHost, static_cast<quint16>(port));
}

void TorRunner::send(const QByteArray &command)
{
    if (m_control)
        m_control->write(command + "\r\n");
}

void TorRunner::onControlReadyRead()
{
    m_buffer.append(m_control->readAll());
    if (m_buffer.size() > kMaxControlReply) {
        fail(tr("Tor's control reply is too long"));
        return;
    }
    qsizetype end;
    while (m_control && (end = m_buffer.indexOf("\r\n")) >= 0) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 2);
        m_reply.append(line);
        // A reply ends with "<code> " (a space after the 3-digit code);
        // "<code>-" lines continue it.
        if (line.size() >= 4 && line.at(3) == ' ') {
            handleReply(std::exchange(m_reply, {}));
        } else if (line.size() >= 4 && line.at(3) == '+') {
            fail(tr("unexpected data reply from Tor"));
            return;
        }
    }
}

void TorRunner::handleReply(const QList<QByteArray> &lines)
{
    if (!lines.last().startsWith("250 ")) {
        fail(tr("Tor refused a request: %1").arg(QString::fromUtf8(lines.last())));
        return;
    }
    switch (m_step) {
    case Step::ProtocolInfo: {
        bool safeCookie = false;
        for (const QByteArray &line : lines) {
            if (line.startsWith("250-AUTH "))
                safeCookie = field(line, "METHODS").split(',').contains("SAFECOOKIE");
        }
        if (!safeCookie) {
            fail(tr("Tor does not offer the expected authentication"));
            return;
        }
        // The cookie is read only from the file this process told tor to use
        // (CookieAuthFile); the server hash below proves that this tor knows
        // its content.
        QFile file(QDir(m_dir).filePath(QStringLiteral("data/control_auth_cookie")));
        if (!file.open(QIODevice::ReadOnly) || (m_cookie = file.read(kCookieBytes + 1)).size() != kCookieBytes) {
            fail(tr("Tor's authentication cookie cannot be read"));
            return;
        }
        m_clientNonce = randomBytes(kNonceBytes);
        m_step = Step::AuthChallenge;
        send("AUTHCHALLENGE SAFECOOKIE " + m_clientNonce.toHex());
        return;
    }
    case Step::AuthChallenge: {
        const QByteArray serverHash = QByteArray::fromHex(field(lines.last(), "SERVERHASH"));
        const QByteArray serverNonce = QByteArray::fromHex(field(lines.last(), "SERVERNONCE"));
        if (serverNonce.size() != kNonceBytes || serverHash.size() != 32) {
            fail(tr("Tor's authentication challenge is not readable"));
            return;
        }
        const QByteArray message = m_cookie + m_clientNonce + serverNonce;
        const QByteArray expected = hmac(kServerHashKey, message);
        // The server proves it knows the cookie before the cookie's proof is sent.
        if (sodium_memcmp(expected.constData(), serverHash.constData(), static_cast<size_t>(expected.size())) != 0) {
            fail(tr("Tor's authentication challenge does not match"));
            return;
        }
        m_step = Step::Authenticate;
        send("AUTHENTICATE " + hmac(kControllerHashKey, message).toHex());
        return;
    }
    case Step::Authenticate:
        m_step = Step::TakeOwnership;
        send("TAKEOWNERSHIP");
        return;
    case Step::TakeOwnership:
        m_step = Step::Socks;
        send("GETINFO net/listeners/socks");
        return;
    case Step::Socks: {
        static const QRegularExpression pattern(QStringLiteral("net/listeners/socks=\"?127\\.0\\.0\\.1:(\\d{1,5})\"?"));
        for (const QByteArray &line : lines) {
            const QRegularExpressionMatch match = pattern.match(QString::fromLatin1(line));
            if (match.hasMatch()) {
                const uint port = match.captured(1).toUInt();
                if (port > 0 && port <= 65535)
                    m_socksPort = static_cast<quint16>(port);
            }
        }
        if (m_socksPort == 0) {
            fail(tr("Tor did not report its SOCKS port"));
            return;
        }
        m_progress = tr("connecting to the Tor network");
        m_step = Step::Bootstrap;
        send("GETINFO status/bootstrap-phase");
        m_bootstrap->start();
        return;
    }
    case Step::Bootstrap: {
        for (const QByteArray &line : lines) {
            if (!line.contains("status/bootstrap-phase="))
                continue;
            const QByteArray percent = field(line, "PROGRESS");
            if (!percent.isEmpty())
                m_progress = tr("connecting to the Tor network (%1%)").arg(QString::fromLatin1(percent));
            if (percent == "100" && field(line, "TAG") == "done") {
                m_bootstrap->stop();
                m_timeout->stop();
                m_step = Step::None;
                m_progress.clear();
                setState(State::Ready);
                return;
            }
        }
        emit stateChanged(); // progress text
        return;
    }
    case Step::None:
        return;
    }
}

void TorRunner::fail(const QString &message)
{
    if (m_state == State::Failed && m_process == nullptr)
        return;
    cleanUp();
    m_error = message;
    m_progress.clear();
    setState(State::Failed);
}

void TorRunner::setState(State state)
{
    m_state = state;
    emit stateChanged();
}

} // namespace app::sellerlist
