#pragma once

#include "model/ModelCatalog.h"
#include "model/Prompts.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

namespace app::model {

// The app's side of the model runner (model/Runner.h): starts the runner
// process, sends one request at a time and kills the runner when a request
// takes longer than its time limit, when it answers something unreadable,
// or when it ends on its own. Every failure ends the runner; the caller
// goes on with the rules.
class ModelClient : public QObject {
    Q_OBJECT
public:
    struct Limits {
        int loadMs = 180 * 1000;
        int identityMs = 30 * 1000;
        int rowMs = 20 * 1000;
    };

    // `program` + `arguments`: the app with "--model-runner" (tests: a fake).
    ModelClient(QString program, QStringList arguments, Limits limits, QObject *parent = nullptr);
    ~ModelClient() override;

    // Starts the runner and loads the model file; loaded() follows.
    void start(const QString &modelPath, const ModelInfo &info, bool gpu);
    bool isReady() const { return m_ready; }
    bool isRunning() const { return m_process != nullptr; }

    // One question; answered() follows (or failed()).
    void ask(const Prompt &prompt, Grammar grammar);
    bool isBusy() const { return m_pending; }

    // Ends the runner (no signal).
    void stop();

signals:
    void loaded(bool ok, const QString &error);
    void answered(const QString &text, qint64 ms);
    // The current request failed; when the runner had to be ended, `ended`.
    void failed(const QString &error, bool ended);

private:
    void send(const QJsonObject &request, int timeoutMs);
    void onReadyRead();
    void onTimeout();
    void onFinished();
    void endWith(const QString &error);

    enum class Waiting { Nothing, Load, Answer };

    QString m_program;
    QStringList m_arguments;
    Limits m_limits;
    QProcess *m_process = nullptr;
    QTimer *m_timer;
    QByteArray m_buffer;
    Waiting m_waiting = Waiting::Nothing;
    bool m_ready = false;
    bool m_pending = false;
};

} // namespace app::model
