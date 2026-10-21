#pragma once

#include <QByteArray>
#include <QNetworkProxy>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTcpSocket;
class QTimer;

namespace app::sellerlist {

// Runs the tor program shipped with the app as a separate process, only
// while the shared seller list needs it, and tells when it can carry
// connections (DESIGN 3.7).
//
// tor gets every setting on its command line (each value one argument, so
// paths with spaces need no quoting), with an empty -f and --defaults-torrc
// in torDir so no torrc of the system or the user is read:
//   DataDirectory <torDir>/data, SocksPort and ControlPort 127.0.0.1:auto,
//   ControlPortWriteToFile <torDir>/control-port, CookieAuthentication 1,
//   CookieAuthFile <torDir>/data/control_auth_cookie,
//   __OwningControllerProcess <this process id>, AvoidDiskWrites 1,
//   ClientOnly 1, Log "notice stdout".
// Then the control port (tor control-spec): PROTOCOLINFO, AUTHCHALLENGE
// SAFECOOKIE with a random client nonce, the server hash checked before
// AUTHENTICATE with the controller hash (HMAC-SHA256, the spec's two key
// strings), TAKEOWNERSHIP (tor exits when this connection closes), GETINFO
// net/listeners/socks for the SOCKS port, then GETINFO
// status/bootstrap-phase once a second until PROGRESS=100 TAG=done.
// Anything else ends in Failed with a message for the user; nothing falls
// back to a direct connection.
class TorRunner : public QObject {
    Q_OBJECT
public:
    enum class State { Off, Starting, Ready, Failed };
    Q_ENUM(State)

    static constexpr int kStartTimeoutMs = 180 * 1000;

    // `program`: the tor executable; `torDir`: the work folder's tor/.
    TorRunner(QString program, QString torDir, QObject *parent = nullptr);
    ~TorRunner() override;

    // The tor shipped with this build (<application folder>/tor/tor[.exe]),
    // or an empty string when this build has none.
    static QString bundledProgram();

    void start();
    void stop();

    State state() const { return m_state; }
    QString error() const { return m_error; }
    QString progress() const { return m_progress; }
    // The SOCKS5 proxy to reach onion services through (valid when Ready).
    QNetworkProxy proxy() const;

signals:
    void stateChanged();

private:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onStartTimeout();
    void pollControlFile();
    void onControlReadyRead();
    void handleReply(const QList<QByteArray> &lines);
    void send(const QByteArray &command);
    void fail(const QString &message);
    void setState(State state);
    void cleanUp();

    enum class Step { None, ProtocolInfo, AuthChallenge, Authenticate, TakeOwnership, Socks, Bootstrap };

    QString m_program;
    QString m_dir;
    QProcess *m_process = nullptr;
    QTcpSocket *m_control = nullptr;
    QTimer *m_poll;
    QTimer *m_timeout;
    QTimer *m_bootstrap;
    State m_state = State::Off;
    Step m_step = Step::None;
    QString m_error;
    QString m_progress;
    QByteArray m_buffer;
    QList<QByteArray> m_reply;
    QByteArray m_cookie;
    QByteArray m_clientNonce;
    quint16 m_socksPort = 0;
    QStringList m_lastOutput;
};

} // namespace app::sellerlist
