#pragma once

#include "core/BrowserProtocol.h"
#include "core/WorkFolder.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QTimer;

namespace app {

// Starts and drives the browser worker: the separate process that runs the
// Qt WebEngine browser for one session (fetches and sign-in) and exits when
// the session ends. The app itself never loads Qt WebEngine, so when no
// session runs, no browser process and no browser memory exist.
//
// Usage: start(); wait for ready(); request() one command at a time, each
// answered by exactly one replied() (a request still open when the worker
// ends is answered with status "failed"); stop() ends the session. finished()
// is emitted once the worker process has exited, however it ended.
//
// The worker connects back over a local socket whose name is random and, on
// macOS and Linux, lives in the work folder's tmp/ folder (owner-only). The
// worker proves it is the process this object started by sending the random
// token given to it on its command line.
class BrowserWorker : public QObject {
    Q_OBJECT
public:
    // `program` is the worker executable. defaultProgram() gives the one
    // installed next to the running executable.
    BrowserWorker(WorkFolder folder, QString program, QObject *parent = nullptr);
    // Stops a running worker (stop(), then waits for it to exit; kills it if
    // it has not exited after kStopTimeoutMs).
    ~BrowserWorker() override;

    BrowserWorker(const BrowserWorker &) = delete;
    BrowserWorker &operator=(const BrowserWorker &) = delete;

    static QString defaultProgram();

    // How long stop() waits for the worker to tear the browser down and exit
    // before it is killed.
    static constexpr int kStopTimeoutMs = 30000;
    // How long start() waits for the worker to connect and report ready.
    static constexpr int kStartTimeoutMs = 60000;
    // The longest socket file path on macOS and Linux: sockaddr_un holds 104
    // bytes on macOS (108 on Linux), including the final zero byte.
    static constexpr int kMaxSocketPathBytes = 103;

    // Starts the worker process. Returns false and sets *error when the
    // local socket cannot be opened or a worker is already running.
    bool start(QString *error);

    // Sends one command (see BrowserProtocol.h; "id" is added here). Returns
    // the request id, or -1 when the worker is not ready or a request is
    // still unanswered.
    int request(QJsonObject command);

    // Asks the worker to tear the browser down and exit; kills it after
    // kStopTimeoutMs. Does nothing when no worker runs.
    void stop();

    // Waits until the worker process has exited (finished() was emitted) or
    // `timeoutMs` passed. Returns true when no worker process runs.
    bool waitForFinished(int timeoutMs);

    bool isRunning() const;
    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_pendingId >= 0; }
    qint64 processId() const;

signals:
    void ready();
    void replied(int id, const QJsonObject &reply);
    // A problem that ends the session: the worker exited unexpectedly, did
    // not connect in time, or broke the protocol. finished() follows.
    void failed(const QString &error);
    // The worker process has exited. `normal` is true when it exited by
    // itself with exit code 0 after a quit request.
    void finished(bool normal, int exitCode);

private:
    void onNewConnection();
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &error);
    void closeServer();

    WorkFolder m_folder;
    QString m_program;
    QString m_token;
    QString m_lastError;
    QProcess *m_process = nullptr;
    QLocalServer *m_server = nullptr;
    QLocalSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    browserprotocol::LineReader m_reader;
    int m_nextId = 1;
    int m_pendingId = -1;
    int m_quitId = -1;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_failed = false;
};

} // namespace app
