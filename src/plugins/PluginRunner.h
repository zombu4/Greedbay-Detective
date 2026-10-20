#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

class QIODevice;

namespace app::plugins {

// Plugin code and plugin .zip files are only ever handled in a separate,
// short-lived process: the runner. The app starts itself with
// "--plugin-runner" (src/app/main.cpp); the tests start a helper built from
// the same code. One request per process: the request comes as JSON on
// standard input, the answer goes as JSON to standard output, then the
// process ends. Whatever a plugin does - loop, use up memory, crash, leave
// work behind in a Promise - ends with that process; the app is never
// touched (DESIGN 5: "a failing plugin is disabled and reported, never
// crashes the app").
//
// Requests:
//   {"op": "check", "script", "fileName", "function", "timeLimitMs"}
//       loads the script; {"ok": true} when it defines `function`
//   {"op": "call", "script", "fileName", "function", "input", "timeLimitMs",
//    "maxAnswerChars"}
//       loads the script and calls `function` with `input` (passed through
//       JSON.parse); {"ok": true, "answer": <its answer as JSON, null for
//       null or undefined>}
//   {"op": "unzip", "path"}
//       reads manifest.json and main.js from the .zip at `path` (at its top
//       or inside one folder; nothing else is read, nothing is written);
//       {"ok": true, "manifest": <base64>, "script": <base64>}
// Any failure: {"ok": false, "error": "<why>"}.
//
// Inside the runner the script runs in a QJSEngine with none of its
// extensions installed and no C++ object passed in, and is stopped by
// QJSEngine::setInterrupted from a watchdog thread after timeLimitMs ("This
// function is thread safe", https://doc.qt.io/qt-6/qjsengine.html).

// How to start the runner: a program and its arguments.
struct RunnerCommand {
    QString program;
    QStringList arguments;
};

// The runner's side: reads one request from `in`, writes one answer to
// `out`. Returns the process exit code (0 when an answer was written).
int runPluginRequest(QIODevice &in, QIODevice &out);

// The same on the process's standard input and output.
int runPluginRequestOnStdio();

// The app's side: starts the runner, sends `request`, and waits for its
// answer for at most `timeLimitMs` plus kStartAllowanceMs (the process has to
// start); a runner still running then is killed. Answers longer than
// `maxAnswerBytes` are refused. The answer object (its "ok" may be false);
// std::nullopt (and *error) when the runner gave none (killed after the
// time limit, crashed, too large, not JSON).
std::optional<QJsonObject> runRequest(const RunnerCommand &command, const QJsonObject &request, int timeLimitMs,
                                      qint64 maxAnswerBytes, QString *error);

// Time for a runner process to start, on top of a request's time limit.
constexpr int kStartAllowanceMs = 5000;

} // namespace app::plugins
