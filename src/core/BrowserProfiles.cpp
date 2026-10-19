#include "core/BrowserProfiles.h"

#include "core/PathChecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>

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

bool BrowserProfiles::isValidId(const QString &browserId)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{1,32}$"));
    return pattern.match(browserId).hasMatch();
}

QString BrowserProfiles::profileDir(const WorkFolder &folder, const QString &browserId)
{
    return folder.browserProfilesDir() + QLatin1Char('/') + browserId;
}

QString BrowserProfiles::lockPath(const WorkFolder &folder, const QString &browserId)
{
    return folder.browserProfilesDir() + QLatin1Char('/') + browserId + QStringLiteral(".lock");
}

QString BrowserProfiles::macCacheTwinDir(const WorkFolder &folder, const QString &browserId)
{
#if defined(Q_OS_MACOS)
    const QString appData = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    const QString caches = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
    const QString profile = QDir::cleanPath(profileDir(folder, browserId));
    if (appData.isEmpty() || caches.isEmpty() || !profile.startsWith(appData + QLatin1Char('/')))
        return {};
    return caches + profile.mid(appData.size());
#else
    Q_UNUSED(folder);
    Q_UNUSED(browserId);
    return {};
#endif
}

bool BrowserProfiles::clear(const WorkFolder &folder, const QString &browserId, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!isValidId(browserId))
        return fail(QStringLiteral("not a browser id; nothing was deleted"));

    const QString dir = profileDir(folder, browserId);
    if (isLink(dir))
        return fail(QStringLiteral("%1 is a link; nothing was deleted").arg(QDir::toNativeSeparators(dir)));
    const QFileInfo info(dir);
    if (!info.exists())
        return true;
    if (!info.isDir())
        return fail(QStringLiteral("%1 is not a folder; nothing was deleted").arg(QDir::toNativeSeparators(dir)));

    QLockFile lock(lockPath(folder, browserId));
    // No age limit, so a long session is never taken for stale; a lock left
    // by a crashed app is still taken over, because QLockFile treats a lock as
    // stale when the process that took it no longer runs.
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError)
            return fail(QStringLiteral("the browser is still running; close it first. Nothing was deleted"));
        return fail(QStringLiteral("cannot create the browser profile lock; nothing was deleted"));
    }

    QString firstError;
    removeContents(dir, &firstError);
    if (!QDir().rmdir(dir) && firstError.isEmpty())
        firstError = QStringLiteral("cannot delete %1").arg(QDir::toNativeSeparators(dir));
    const QString twin = macCacheTwinDir(folder, browserId);
    if (!twin.isEmpty() && QFileInfo(twin).isDir() && !isLink(twin)) {
        removeContents(twin, &firstError);
        if (!QDir().rmdir(twin) && firstError.isEmpty())
            firstError = QStringLiteral("cannot delete %1").arg(QDir::toNativeSeparators(twin));
    }
    lock.unlock();
    if (!firstError.isEmpty())
        return fail(firstError);
    return true;
}

} // namespace app
