#include "plugins/PluginRunner.h"

#include "plugins/PluginManifest.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
// Qt's own ZIP reader. It is private Qt API (no compatibility promise
// between Qt versions); the app builds against one pinned Qt version and
// ships it (CMakeLists.txt, QT_NO_PRIVATE_MODULE_WARNING).
#include <QtCore/private/qzipreader_p.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <tuple>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace app::plugins {

namespace {

// The largest request a runner reads.
constexpr qint64 kMaxRequestBytes = 16 * 1024 * 1024;

// Stops the engine's script after `ms` milliseconds unless stop() comes
// first. QJSEngine::setInterrupted is thread safe (Qt docs); the watchdog
// thread calls nothing else.
class Watchdog {
public:
    Watchdog(QJSEngine *engine, int ms)
        : m_thread([this, engine, ms]() {
              std::unique_lock<std::mutex> lock(m_mutex);
              if (!m_wake.wait_for(lock, std::chrono::milliseconds(ms), [this]() { return m_stopped; })) {
                  m_fired = true;
                  engine->setInterrupted(true);
              }
          })
    {
    }
    ~Watchdog() { stop(); }
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;

    // Ends the watch; true when the time limit was reached.
    bool stop()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        return m_fired;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopped = false;
    bool m_fired = false;
    std::thread m_thread;
};

QJsonObject failure(const QString &why)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), why}};
}

// The error the engine holds, or `value` when it is an error; empty when
// there is none.
QString errorOf(QJSEngine &engine, const QJSValue &value)
{
    if (engine.hasError())
        return engine.catchError().toString();
    return value.isError() ? value.toString() : QString();
}

// "check" and "call".
QJsonObject runScript(const QJsonObject &request, bool call)
{
    const QString script = request.value(QStringLiteral("script")).toString();
    const QString fileName = request.value(QStringLiteral("fileName")).toString();
    const QString function = request.value(QStringLiteral("function")).toString();
    const int limit = request.value(QStringLiteral("timeLimitMs")).toInt();
    if (function.isEmpty() || limit <= 0)
        return failure(QStringLiteral("the request names no function or no time limit"));

    // No installExtensions(): no console, no qsTr, no gc().
    QJSEngine engine;
    const QJSValue json = engine.globalObject().property(QStringLiteral("JSON"));
    // Taken before the script runs, so it cannot replace them.
    const QJSValue parse = json.property(QStringLiteral("parse"));
    const QJSValue stringify = json.property(QStringLiteral("stringify"));

    Watchdog watchdog(&engine, limit);
    const QJSValue loaded = engine.evaluate(script, fileName);
    if (watchdog.stop())
        return failure(QStringLiteral("main.js did not finish loading within %1 ms").arg(limit));
    if (const QString thrown = errorOf(engine, loaded); !thrown.isEmpty())
        return failure(QStringLiteral("main.js does not load: %1").arg(thrown));
    const QJSValue callable = engine.globalObject().property(function);
    if (!callable.isCallable())
        return failure(QStringLiteral("main.js defines no function %1()").arg(function));
    if (!call)
        return QJsonObject{{QStringLiteral("ok"), true}};

    const QByteArray input = QJsonDocument(request.value(QStringLiteral("input")).toObject()).toJson(QJsonDocument::Compact);
    // One watch over parsing the input, the call and turning the answer into
    // JSON (an interrupted step makes every later one abort at once).
    Watchdog callWatchdog(&engine, limit);
    const QJSValue argument = parse.call({QString::fromUtf8(input)});
    const QJSValue answer = callable.call({argument});
    const bool empty = answer.isUndefined() || answer.isNull();
    const QJSValue text = empty || answer.isError() ? QJSValue() : stringify.call({answer});
    if (callWatchdog.stop())
        return failure(QStringLiteral("it did not finish within %1 ms").arg(limit));
    if (const QString thrown = errorOf(engine, answer.isError() ? answer : text); !thrown.isEmpty())
        return failure(QStringLiteral("it stopped with an error: %1").arg(thrown));
    if (empty)
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("answer"), QJsonValue(QJsonValue::Null)}};
    const QString out = text.toString();
    const qint64 maxAnswer = request.value(QStringLiteral("maxAnswerChars")).toInteger();
    if (!text.isString() || out.size() > maxAnswer)
        return failure(QStringLiteral("its answer is not plain data or larger than %1 KB").arg(maxAnswer / 1024));
    // Wrapped in an array so any JSON value parses.
    const QJsonDocument document = QJsonDocument::fromJson(QStringLiteral("[%1]").arg(out).toUtf8());
    if (!document.isArray() || document.array().size() != 1)
        return failure(QStringLiteral("its answer is not plain data"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("answer"), document.array().first()}};
}

// "unzip": manifest.json and main.js at the top of the .zip or inside one
// folder; nothing else is read.
QJsonObject unzip(const QJsonObject &request)
{
    const QString path = request.value(QStringLiteral("path")).toString();
    const QString manifestName = QStringLiteral("manifest.json");
    const QString scriptName = QStringLiteral("main.js");
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return failure(QStringLiteral("it is not a readable .zip file"));
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    // Where manifest.json is: "" (the top) or "<folder>/".
    QStringList prefixes;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.filePath == manifestName)
            prefixes.append(QString());
        else if (entry.filePath.endsWith(QLatin1Char('/') + manifestName) && entry.filePath.count(QLatin1Char('/')) == 1)
            prefixes.append(entry.filePath.chopped(manifestName.size()));
    }
    if (prefixes.size() != 1)
        return failure(QStringLiteral("the .zip must hold one manifest.json, at its top or inside one folder"));
    const QString prefix = prefixes.first();
    QJsonObject answer{{QStringLiteral("ok"), true}};
    for (const auto &[name, limit, key] : {std::tuple{manifestName, kMaxManifestBytes, QStringLiteral("manifest")},
                                           std::tuple{scriptName, kMaxScriptBytes, QStringLiteral("script")}}) {
        const auto entry = std::find_if(entries.cbegin(), entries.cend(),
                                        [&](const QZipReader::FileInfo &info) { return info.filePath == prefix + name; });
        if (entry == entries.cend() || !entry->isFile || entry->isSymLink)
            return failure(QStringLiteral("%1 is missing from the .zip or is not a plain file").arg(prefix + name));
        if (entry->size > limit)
            return failure(QStringLiteral("%1 in the .zip is larger than %2 KB").arg(prefix + name).arg(limit / 1024));
        const QByteArray data = zip.fileData(prefix + name);
        if (zip.status() != QZipReader::NoError || data.size() > limit)
            return failure(QStringLiteral("%1 cannot be read from the .zip").arg(prefix + name));
        answer.insert(key, QString::fromLatin1(data.toBase64()));
    }
    return answer;
}

} // namespace

int runPluginRequest(QIODevice &in, QIODevice &out)
{
    const QByteArray data = in.read(kMaxRequestBytes + 1);
    QJsonObject answer;
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (data.size() > kMaxRequestBytes || !document.isObject()) {
        answer = failure(QStringLiteral("the request is not a JSON object of at most %1 MB").arg(kMaxRequestBytes / (1024 * 1024)));
    } else {
        const QJsonObject request = document.object();
        const QString op = request.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("check"))
            answer = runScript(request, false);
        else if (op == QLatin1String("call"))
            answer = runScript(request, true);
        else if (op == QLatin1String("unzip"))
            answer = unzip(request);
        else
            answer = failure(QStringLiteral("unknown request \"%1\"").arg(op));
    }
    const QByteArray bytes = QJsonDocument(answer).toJson(QJsonDocument::Compact);
    return out.write(bytes) == bytes.size() ? 0 : 1;
}

int runPluginRequestOnStdio()
{
#if defined(Q_OS_WIN)
    // Bytes as they are: no text-mode translation of line ends or Ctrl-Z.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile in;
    QFile out;
    if (!in.open(stdin, QIODevice::ReadOnly) || !out.open(stdout, QIODevice::WriteOnly))
        return 1;
    const int code = runPluginRequest(in, out);
    out.flush();
    return code;
}

std::optional<QJsonObject> runRequest(const RunnerCommand &command, const QJsonObject &request, int timeLimitMs,
                                      qint64 maxAnswerBytes, QString *error)
{
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(command.program, command.arguments);
    if (!process.waitForStarted(kStartAllowanceMs)) {
        *error = QStringLiteral("the plugin runner cannot be started (%1)").arg(process.errorString());
        return std::nullopt;
    }
    const QByteArray bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    process.write(bytes);
    process.closeWriteChannel();
    QByteArray answer;
    const QDeadlineTimer deadline(timeLimitMs + kStartAllowanceMs);
    bool tooLarge = false;
    while (process.state() != QProcess::NotRunning && !deadline.hasExpired() && !tooLarge) {
        process.waitForReadyRead(static_cast<int>(std::max<qint64>(1, std::min<qint64>(deadline.remainingTime(), 100))));
        answer += process.readAllStandardOutput();
        tooLarge = answer.size() > maxAnswerBytes;
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(kStartAllowanceMs);
        *error = tooLarge ? QStringLiteral("its answer is larger than %1 KB").arg(maxAnswerBytes / 1024)
                          : QStringLiteral("it did not finish within %1 ms").arg(timeLimitMs);
        return std::nullopt;
    }
    answer += process.readAllStandardOutput();
    if (answer.size() > maxAnswerBytes) {
        *error = QStringLiteral("its answer is larger than %1 KB").arg(maxAnswerBytes / 1024);
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        *error = QStringLiteral("its process ended abnormally (out of memory or a crash)");
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(answer);
    if (!document.isObject()) {
        *error = QStringLiteral("its process gave no answer (exit code %1)").arg(process.exitCode());
        return std::nullopt;
    }
    return document.object();
}

} // namespace app::plugins
