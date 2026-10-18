#pragma once

#include <QString>
#include <QtGlobal>

namespace app {

// Rotating text log in <work>/logs (PLAN section 3).
//
// The current file is <appIdentifier>.log; older ones are <appIdentifier>.1.log (newest)
// to <appIdentifier>.4.log (oldest). Each line is
//   <UTC ISO-8601 time with ms> <level> [<category>] <message>
// ending in "\n" on every platform. A message longer than kMaxLineBytes is
// cut, so no file grows beyond kMaxFileBytes and at most kMaxFiles exist.
//
// If a rotation fails (for example because another program holds a log file
// open on Windows), nothing is deleted, every rename already done is undone,
// and rotation is not tried again until the next install(): the current file
// may then grow past kMaxFileBytes, but no log is lost.
class Logging {
public:
    static constexpr qint64 kMaxFileBytes = 1024 * 1024;
    static constexpr qint64 kMaxLineBytes = 64 * 1024;
    static constexpr int kMaxFiles = 5;

    // Installs a Qt message handler that writes every qDebug/qInfo/qWarning/
    // qCritical/qFatal message to the log and passes it on to the previously
    // installed handler. Returns false and sets *error when the log file
    // cannot be opened or is a link. Installing again first uninstalls.
    static bool install(const QString &logsDir, const QString &appIdentifier, QString *error);

    // Restores the previous message handler and closes the file.
    static void uninstall();

    // Shifts the files by one: <appIdentifier>.4.log is deleted, .3 becomes .4, …,
    // and the current file becomes <appIdentifier>.1.log. Only these exact names
    // are touched, and only regular files (never links). The oldest file is
    // deleted only after every rename succeeded; on failure the renames are
    // undone and false is returned. The handler calls this while the current
    // file is closed.
    static bool rotate(const QString &logsDir, const QString &appIdentifier, QString *error);

    static QString currentFileName(const QString &appIdentifier);
    static QString rotatedFileName(const QString &appIdentifier, int index);
};

} // namespace app
