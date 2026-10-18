#pragma once

#include "browser/BrowserSession.h"
#include "core/BrowserProtocol.h"
#include "core/WorkFolder.h"

#include <QJsonObject>
#include <QObject>

#include <memory>

class QLocalSocket;

namespace app::browser {

// The browser worker's side of the protocol in core/BrowserProtocol.h: it
// connects to the app's local socket, creates the browser session on the
// first fetch or sign-in request, answers each request, and on "quit" tears
// the session down, confirms, and ends the process. When the connection
// closes for any other reason (the app ended or crashed), it tears the
// session down too and ends the process with exit code kExitConnectionLost.
class WorkerHost : public QObject {
    Q_OBJECT
public:
    static constexpr int kExitConnectionLost = 3;

    WorkerHost(WorkFolder folder, SessionOptions options, QObject *parent = nullptr);
    ~WorkerHost() override;

    // Connects to the app (waits up to 30 s) and sends the ready message
    // with `token`.
    bool connectToApp(const QString &serverName, const QString &token, QString *error);

private:
    void onReadyRead();
    void handle(const QJsonObject &message);
    bool ensureSession(int id);
    void send(const QJsonObject &message);
    void reply(int id, QJsonObject message);
    void quit(int id);
    void onDisconnected();

    WorkFolder m_folder;
    SessionOptions m_options;
    QLocalSocket *m_socket = nullptr;
    browserprotocol::LineReader m_reader;
    std::unique_ptr<BrowserSession> m_session;
    int m_currentId = -1;
    bool m_quitting = false;
};

} // namespace app::browser
