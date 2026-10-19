#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QTimer;
class QWebSocket;

namespace app::browser {

// Starts a Chromium-based browser (Brave, Chrome, Edge, Chromium) with remote
// debugging on a random local port and talks the Chrome DevTools Protocol
// with it over a WebSocket.
//
// Why a port and not --remote-debugging-pipe: Chromium marks every page as
// automated (navigator.webdriver = true) when it runs with the pipe switch
// (content/child/web_runtime_features.cc maps kRemoteDebuggingPipe to
// EnableAutomationControlled; measured with Brave 155 on macOS: true), and a
// site sees that marker. With a fixed, non-zero port the marker stays false
// (measured: false). The cost: while the session runs, other programs on the
// same computer - including processes of other users of the computer -
// could connect to that port and drive that browser session. Also, the port
// is chosen free and released just before the browser takes it; a program
// that grabs it in that moment could pose as the browser. Mitigations: the
// port is random and bound to 127.0.0.1 only, the browser runs only during a
// session, and it uses a profile folder of its own in the work folder
// (Chrome 136+ ignores remote debugging on a browser's default profile).
//
// The WebSocket URL of the browser target comes from the browser's own
// answer at http://127.0.0.1:<port>/json/version (webSocketDebuggerUrl); it is
// accepted only for 127.0.0.1, that port and a /devtools/browser/ path.
// (Measured with Brave 155: with an explicit port the browser listens on
// 127.0.0.1 only and writes no DevToolsActivePort file.)
//
// Commands are sent with send() and answered by response() with the same id.
// Browser-level commands have no session id; page commands carry the session
// id from Target.attachToTarget (flatten mode). Events arrive as cdpEvent().
class CdpConnection : public QObject {
    Q_OBJECT
public:
    // How long start() waits for the browser to open its DevTools port.
    static constexpr int kConnectTimeoutMs = 30000;
    // A message longer than this is a protocol error (extractor results for a
    // page of 240 search results are far below it).
    static constexpr qsizetype kMaxMessageBytes = 64 * 1024 * 1024;

    explicit CdpConnection(QObject *parent = nullptr);
    ~CdpConnection() override;

    CdpConnection(const CdpConnection &) = delete;
    CdpConnection &operator=(const CdpConnection &) = delete;

    // Starts `program` with `arguments` plus --remote-debugging-port=<random>
    // and connects once the browser answers on that port; connected() or
    // protocolError() follows. Returns false and sets *error when no port can
    // be chosen or the process cannot be started.
    bool start(const QString &program, const QStringList &arguments, QString *error);

    // Tests only: before start(), a file for the browser's error output
    // (otherwise discarded).
    void setErrorFile(const QString &path) { m_errorFile = path; }

    // Sends one command; returns its id (> 0), or -1 when not connected.
    int send(const QString &method, const QJsonObject &params = {}, const QString &sessionId = {});

    // Waits up to `timeoutMs` for the browser process to exit by itself
    // (after Browser.close), then kills it. Returns true when it exited by
    // itself (or was not running).
    bool waitForExit(int timeoutMs);

    bool isRunning() const;
    bool isConnected() const { return m_connected; }
    qint64 processId() const;
    // Pushes queued messages out now (used before blocking waits).
    void flush();

    // Parses one DevTools message. std::nullopt (and *error) when it is not
    // a JSON object or is longer than kMaxMessageBytes.
    static std::optional<QJsonObject> parseMessage(const QByteArray &message, QString *error);

    // Reads the browser's /json/version answer: its webSocketDebuggerUrl when
    // that is ws://127.0.0.1:<port>/devtools/browser/<id>; otherwise an empty
    // URL.
    static QUrl webSocketUrlFromVersion(const QByteArray &json, int port);

signals:
    void connected();
    void response(int id, const QJsonObject &result, const QJsonObject &error);
    void cdpEvent(const QString &method, const QJsonObject &params, const QString &sessionId);
    // The connection could not be made, broke, or carried something that is
    // not a protocol message.
    void protocolError(const QString &error);
    // The browser process has exited.
    void finished(int exitCode, bool crashed);

private:
    void pollVersion();
    void connectWebSocket(const QUrl &url);
    void onTextMessage(const QString &message);

    QProcess *m_process = nullptr;
    QWebSocket *m_socket = nullptr;
    QTimer *m_pollTimer = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    bool m_polling = false;
    QString m_errorFile;
    int m_port = 0;
    int m_pollsLeft = 0;
    bool m_connected = false;
    int m_nextId = 1;
};

} // namespace app::browser
