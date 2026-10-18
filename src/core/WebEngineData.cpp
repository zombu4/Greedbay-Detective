#include "core/WebEngineData.h"

#include "core/PathChecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

namespace app {

namespace {

// Removes a link itself, never its target. A symbolic link to a file (and on
// Unix any symbolic link) is removed with QFile::remove; a Windows junction or
// directory symbolic link is a directory entry and is removed with rmdir,
// which deletes the link, not the folder it points to.
bool removeLink(const QString &path)
{
    return QFile::remove(path) || QDir().rmdir(path);
}

// Deletes everything inside `dir` without following links. Keeps going after
// a failure so as much as possible is removed; the first failure is reported.
void removeContents(const QString &dir, QString *firstError)
{
    const QFileInfoList entries =
        QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.filePath();
        bool ok = true;
        if (isLink(path)) {
            ok = removeLink(path);
        } else if (entry.isDir()) {
            removeContents(path, firstError);
            ok = QDir().rmdir(path);
        } else {
            ok = QFile::remove(path);
        }
        if (!ok && firstError->isEmpty())
            *firstError = QStringLiteral("cannot delete %1").arg(QDir::toNativeSeparators(path));
    }
}

} // namespace

QString WebEngineData::sessionLockPath(const WorkFolder &folder)
{
    return folder.path() + QStringLiteral("/webengine/session.lock");
}

bool WebEngineData::clear(const WorkFolder &folder, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const QString root = folder.path() + QStringLiteral("/webengine");
    if (isLink(root))
        return fail(QStringLiteral("%1 is a link; nothing was deleted").arg(QDir::toNativeSeparators(root)));
    if (!QFileInfo(root).isDir())
        return fail(QStringLiteral("%1 is not a folder; nothing was deleted").arg(QDir::toNativeSeparators(root)));

    const QString lockPath = sessionLockPath(folder);
    QLockFile lock(lockPath);
    // Same settings as the worker's lock: no age limit, so a long session is
    // never taken for stale; a lock left by a crashed worker is still taken
    // over, because QLockFile treats a lock as stale when the process that
    // took it no longer runs.
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError)
            return fail(QStringLiteral("a browser session is running; nothing was deleted"));
        return fail(QStringLiteral("cannot create the browser session lock; nothing was deleted"));
    }

    QString firstError;
    const QFileInfoList entries =
        QDir(root).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.filePath();
        if (QFileInfo(path).fileName() == QFileInfo(lockPath).fileName())
            continue;
        bool ok = true;
        if (isLink(path)) {
            ok = removeLink(path);
        } else if (entry.isDir()) {
            removeContents(path, &firstError);
            ok = QDir().rmdir(path);
        } else {
            ok = QFile::remove(path);
        }
        if (!ok && firstError.isEmpty())
            firstError = QStringLiteral("cannot delete %1").arg(QDir::toNativeSeparators(path));
    }
    const QString dirs[] = {folder.webEngineStorageDir(), folder.webEngineCacheDir(), folder.webEngineDownloadsDir()};
    for (const QString &dir : dirs) {
        if (!QFileInfo(dir).isDir() && !QDir().mkpath(dir) && firstError.isEmpty())
            firstError = QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(dir));
    }
    lock.unlock();
    if (!firstError.isEmpty())
        return fail(firstError);
    return true;
}

} // namespace app
