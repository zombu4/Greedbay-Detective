#include "model/ModelClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace app::model {

namespace {

constexpr qsizetype kMaxReplyBytes = 64 * 1024;

} // namespace

ModelClient::ModelClient(QString program, QStringList arguments, Limits limits, QObject *parent)
    : QObject(parent), m_program(std::move(program)), m_arguments(std::move(arguments)), m_limits(limits), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ModelClient::onTimeout);
}

ModelClient::~ModelClient() { stop(); }

void ModelClient::start(const QString &modelPath, const ModelInfo &info, bool gpu)
{
    stop();
    m_process = new QProcess(this);
    m_process->setProgram(m_program);
    m_process->setArguments(m_arguments);
    // The runner's own messages are not needed; its answers come on stdout.
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ModelClient::onReadyRead);
    connect(m_process, &QProcess::finished, this, &ModelClient::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            endWith(QStringLiteral("the model runner could not be started"));
    });
    m_waiting = Waiting::Load;
    m_process->start();
    if (!m_process)
        return; // it failed to start; loaded(false) was sent
    send({{"op", "load"},
          {"path", modelPath},
          {"sha256", info.sha256},
          {"gpu", gpu},
          {"format", info.format == PromptFormat::Qwen3NoThinking ? QStringLiteral("qwen3") : QStringLiteral("chatml")}},
         m_limits.loadMs);
}

void ModelClient::ask(const Prompt &prompt, Grammar grammar)
{
    if (!m_ready || m_pending) {
        emit failed(QStringLiteral("the model runner is not ready"), false);
        return;
    }
    QJsonArray pieces;
    for (const PromptPiece &piece : prompt)
        pieces.append(QJsonObject{{"text", piece.text}, {"special", piece.special}});
    m_waiting = Waiting::Answer;
    send({{"op", "generate"},
          {"prompt", pieces},
          {"grammar", grammar == Grammar::Identity ? QStringLiteral("identity") : QStringLiteral("sort")}},
         grammar == Grammar::Identity ? m_limits.identityMs : m_limits.rowMs);
}

void ModelClient::send(const QJsonObject &request, int timeoutMs)
{
    if (!m_process)
        return;
    m_pending = true;
    m_timer->start(timeoutMs);
    m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void ModelClient::onReadyRead()
{
    m_buffer.append(m_process->readAllStandardOutput());
    if (m_buffer.size() > kMaxReplyBytes) {
        endWith(QStringLiteral("the model runner's answer is too long"));
        return;
    }
    const qsizetype end = m_buffer.indexOf('\n');
    if (end < 0)
        return;
    const QByteArray line = m_buffer.left(end);
    m_buffer.remove(0, end + 1);
    if (!m_buffer.isEmpty() || m_waiting == Waiting::Nothing) {
        endWith(QStringLiteral("the model runner answered out of turn"));
        return;
    }
    m_timer->stop();
    m_pending = false;
    const Waiting waiting = std::exchange(m_waiting, Waiting::Nothing);
    const QJsonObject reply = QJsonDocument::fromJson(line).object();
    const bool ok = reply.value(QStringLiteral("ok")).toBool();
    const QString error = reply.value(QStringLiteral("error")).toString();
    if (waiting == Waiting::Load) {
        if (!ok) {
            const QString why = error.isEmpty() ? QStringLiteral("the model could not be loaded") : error;
            stop();
            emit loaded(false, why);
            return;
        }
        m_ready = true;
        emit loaded(true, {});
        return;
    }
    if (!ok || !reply.value(QStringLiteral("text")).isString()) {
        emit failed(error.isEmpty() ? QStringLiteral("the model gave no answer") : error, false);
        return;
    }
    emit answered(reply.value(QStringLiteral("text")).toString(), reply.value(QStringLiteral("ms")).toInteger());
}

void ModelClient::onTimeout() { endWith(QStringLiteral("the local model took too long; it was stopped")); }

void ModelClient::onFinished()
{
    if (m_process)
        endWith(QStringLiteral("the model runner ended unexpectedly"));
}

void ModelClient::endWith(const QString &error)
{
    const Waiting waiting = m_waiting;
    stop();
    if (waiting == Waiting::Load)
        emit loaded(false, error);
    else
        emit failed(error, true);
}

void ModelClient::stop()
{
    m_timer->stop();
    m_buffer.clear();
    m_waiting = Waiting::Nothing;
    m_ready = false;
    m_pending = false;
    if (!m_process)
        return;
    QProcess *process = std::exchange(m_process, nullptr);
    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        // End of input ends the runner; a runner that does not end is killed.
        process->closeWriteChannel();
        if (!process->waitForFinished(2000)) {
            process->kill();
            process->waitForFinished(2000);
        }
    }
    process->deleteLater();
}

} // namespace app::model
