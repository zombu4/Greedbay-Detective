#include "core/Logging.h"

#include "core/PathChecks.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <utility>
#include <vector>

namespace app {

namespace {

struct LogState {
    QMutex mutex;
    QFile file;
    QString logsDir;
    QString appName;
    QtMessageHandler previous = nullptr;
    bool installed = false;
    bool rotationBlocked = false;
};

// Allocated once and never destroyed: a message emitted while static objects
// are destroyed at exit must not reach a destroyed mutex or file.
LogState &logState()
{
    static LogState *state = new LogState;
    return *state;
}

// Set while this thread is inside the handler. A message emitted by a Qt call
// made inside the handler is passed on without being logged, instead of
// deadlocking on the (non-recursive) mutex.
thread_local bool insideHandler = false;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "unknown";
}

QString logPath(const QString &logsDir, const QString &appName, int index)
{
    return logsDir + QLatin1Char('/')
           + (index == 0 ? Logging::currentFileName(appName) : Logging::rotatedFileName(appName, index));
}

// Caller holds the mutex.
bool openCurrentFile(LogState &state, QString *error)
{
    const QString path = logPath(state.logsDir, state.appName, 0);
    if (isLink(path) || (somethingAt(path) && !isPlainFile(path))) {
        if (error)
            *error = QStringLiteral("the log file is a link or not a regular file; it is not used");
        return false;
    }
    state.file.setFileName(path);
    // Binary mode: lines end in "\n" everywhere, so sizes are counted exactly.
    if (!state.file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error)
            *error = QStringLiteral("cannot open the log file: %1").arg(state.file.errorString());
        return false;
    }
    return true;
}

QByteArray formatLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QByteArray text = message.toUtf8();
    static const QByteArray cutNote = QByteArrayLiteral(" [cut]");
    const qsizetype room = Logging::kMaxLineBytes - 128; // timestamp, level, category
    if (text.size() > room) {
        text.truncate(room - cutNote.size());
        text += cutNote;
    }
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + ' ' + levelName(type)
           + " [" + QByteArray(context.category ? context.category : "default").left(64) + "] " + text
           + '\n';
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LogState &state = logState();
    QtMessageHandler previous = nullptr;
    if (!insideHandler) {
        insideHandler = true;
        QMutexLocker lock(&state.mutex);
        previous = state.previous;
        if (state.file.isOpen()) {
            const QByteArray line = formatLine(type, context, message);
            if (!state.rotationBlocked && state.file.size() > 0
                && state.file.size() + line.size() > Logging::kMaxFileBytes) {
                // Rotate while the file is closed: Windows cannot rename an open file.
                state.file.close();
                QString ignored;
                if (!Logging::rotate(state.logsDir, state.appName, &ignored))
                    state.rotationBlocked = true;
                openCurrentFile(state, &ignored);
            }
            if (state.file.isOpen()) {
                state.file.write(line);
                state.file.flush();
            }
        }
        insideHandler = false;
    } else {
        previous = state.previous;
    }
    // The previous handler (Qt's default prints to stderr) still sees every
    // message; for QtFatalMsg Qt aborts after the handlers return.
    if (previous)
        previous(type, context, message);
}

} // namespace

QString Logging::currentFileName(const QString &appName)
{
    return appName + QStringLiteral(".log");
}

QString Logging::rotatedFileName(const QString &appName, int index)
{
    return appName + QLatin1Char('.') + QString::number(index) + QStringLiteral(".log");
}

bool Logging::install(const QString &logsDir, const QString &appName, QString *error)
{
    uninstall();
    LogState &state = logState();
    {
        QMutexLocker lock(&state.mutex);
        state.logsDir = logsDir;
        state.appName = appName;
        state.rotationBlocked = false;
        if (!openCurrentFile(state, error))
            return false;
    }
    QtMessageHandler previous = qInstallMessageHandler(messageHandler);
    QMutexLocker lock(&state.mutex);
    state.previous = previous;
    state.installed = true;
    return true;
}

void Logging::uninstall()
{
    LogState &state = logState();
    QtMessageHandler previous = nullptr;
    bool installed = false;
    {
        QMutexLocker lock(&state.mutex);
        installed = state.installed;
        previous = state.previous;
        state.installed = false;
        state.previous = nullptr;
        state.file.close();
    }
    if (installed)
        qInstallMessageHandler(previous);
}

bool Logging::rotate(const QString &logsDir, const QString &appName, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    // Every file that exists must be a regular file before anything changes.
    for (int index = 0; index < kMaxFiles; ++index) {
        const QString path = logPath(logsDir, appName, index);
        if (somethingAt(path) && !isPlainFile(path))
            return fail(QStringLiteral("%1 is not a regular file; nothing was rotated").arg(path));
    }

    const QString oldest = logPath(logsDir, appName, kMaxFiles - 1);
    const QString aside = oldest + QStringLiteral(".old");
    // Left over from an earlier rotation whose final delete failed.
    if (somethingAt(aside)) {
        if (!isPlainFile(aside) || !QFile::remove(aside))
            return fail(QStringLiteral("cannot remove %1").arg(aside));
    }

    // Renames done so far, as (from, to), to undo on failure.
    std::vector<std::pair<QString, QString>> done;
    auto undo = [&done]() {
        for (auto it = done.rbegin(); it != done.rend(); ++it)
            QFile::rename(it->second, it->first);
    };
    auto move = [&](const QString &from, const QString &to) {
        if (!QFile::rename(from, to))
            return false;
        done.emplace_back(from, to);
        return true;
    };

    if (somethingAt(oldest) && !move(oldest, aside))
        return fail(QStringLiteral("cannot move %1 aside").arg(oldest));
    for (int index = kMaxFiles - 2; index >= 0; --index) {
        const QString from = logPath(logsDir, appName, index);
        if (!somethingAt(from))
            continue;
        if (!move(from, logPath(logsDir, appName, index + 1))) {
            undo();
            return fail(QStringLiteral("cannot rename %1; the rotation was undone").arg(from));
        }
    }
    // Everything moved: only now is the oldest file deleted. If that fails it
    // stays as <name>.old and is removed by the next rotation.
    if (somethingAt(aside))
        QFile::remove(aside);
    return true;
}

} // namespace app
