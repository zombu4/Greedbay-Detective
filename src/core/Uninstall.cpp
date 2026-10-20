#include "core/Uninstall.h"

#include "core/PathChecks.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>

namespace app {

namespace {

QString shown(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

// Removes `path`: a link as a link, a folder with everything in it (never
// following a link inside), a file. Failures go to *failed; returns true
// when `path` is gone.
bool removeEntry(const QString &path, QStringList *failed)
{
    const QFileInfo info(path);
    if (isLink(path)) {
#if defined(Q_OS_WIN)
        // On Windows a junction or a link to a folder is a folder entry of its
        // own: removing it like an empty folder leaves its target alone.
        const bool removed = (info.isJunction() || info.isDir()) ? QDir().rmdir(path) : QFile::remove(path);
#else
        // Elsewhere every link is a file entry; unlinking it leaves its target
        // alone.
        const bool removed = QFile::remove(path);
#endif
        if (!removed)
            failed->append(QStringLiteral("%1: the link cannot be removed").arg(shown(path)));
        return removed;
    }
    if (info.isDir()) {
        bool all = true;
        const QDir dir(path);
        const QStringList names = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QString &name : names)
            all = removeEntry(dir.filePath(name), failed) && all;
        if (all && !QDir().rmdir(path)) {
            failed->append(QStringLiteral("%1: the folder cannot be removed").arg(shown(path)));
            return false;
        }
        return all;
    }
    if (!QFile::remove(path)) {
        failed->append(QStringLiteral("%1: the file cannot be removed").arg(shown(path)));
        return false;
    }
    return true;
}

} // namespace

QStringList Uninstall::keptNames(const QString &appIdentifier)
{
    // The database's rollback journal too: after a crash it is "hot", and
    // the database needs it to roll back (the app uses SQLite's default
    // rollback journal, PLAN 3).
    return {appIdentifier + QStringLiteral(".sqlite"), appIdentifier + QStringLiteral(".sqlite-journal"), QStringLiteral("backups"),
            QString::fromLatin1(WorkFolder::kMarkerFileName), QStringLiteral("known-models.json")};
}

QStringList Uninstall::knownPaths(const QString &appIdentifier)
{
#if defined(Q_OS_MACOS)
    const QString caches = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (caches.isEmpty())
        return {};
    return {QDir::cleanPath(caches + QLatin1Char('/') + appIdentifier)};
#else
    Q_UNUSED(appIdentifier);
    return {};
#endif
}

QString Uninstall::check(const WorkFolder &folder)
{
    const QString path = QDir::cleanPath(folder.path());
    const QString expected = QDir::cleanPath(WorkFolder::defaultPath(folder.appIdentifier()));
    if (expected.isEmpty() || path != expected)
        return QStringLiteral("%1 is not the app's own work folder").arg(shown(path));
    if (path == QDir::cleanPath(QDir::homePath()) || QDir(path).isRoot())
        return QStringLiteral("%1 is a home folder or a root").arg(shown(path));
    if (isLink(path) || !QFileInfo(path).isDir())
        return QStringLiteral("%1 is a link or not a folder").arg(shown(path));
    QFile marker(folder.markerPath());
    if (!isPlainFile(folder.markerPath()) || !marker.open(QIODevice::ReadOnly)
        || !WorkFolder::isValidMarkerId(QString::fromLatin1(marker.read(64)).trimmed()))
        return QStringLiteral("%1 has no valid marker, so it is not known to be the app's folder").arg(shown(path));
    QLockFile lock(folder.lockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return QStringLiteral("the app is running (its lock is held); close it first");
    lock.unlock();
    return {};
}

Uninstall::Report Uninstall::run(const WorkFolder &folder, bool keepHistory)
{
    Report report;
    // The app's own lock, held until everything else is gone: no instance
    // of the app can start on the folder while it is being removed.
    QLockFile lock(folder.lockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        report.failed.append(QStringLiteral("%1: the app is running (its lock is held); nothing was removed").arg(shown(folder.path())));
        return report;
    }
    const QDir root(folder.path());
    const QStringList kept = keepHistory ? keptNames(folder.appIdentifier()) : QStringList();
    const QString marker = QString::fromLatin1(WorkFolder::kMarkerFileName);
    const QString lockName = QString::fromLatin1(WorkFolder::kLockFileName);
    QStringList names = root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    names.removeAll(marker);
    names.removeAll(lockName);
    for (const QString &name : std::as_const(names)) {
        const QString path = root.filePath(name);
        if (kept.contains(name)) {
            report.kept.append(shown(path));
            continue;
        }
        if (!somethingAt(path))
            continue;
        if (removeEntry(path, &report.failed))
            report.removed.append(shown(path));
    }
    // Then the lock (unlock() deletes its file), and the marker last: until
    // everything else is gone the folder stays recognizable as the app's.
    lock.unlock();
    if (!keepHistory && somethingAt(folder.markerPath()) && report.ok()) {
        if (removeEntry(folder.markerPath(), &report.failed))
            report.removed.append(shown(folder.markerPath()));
    }
    if (keepHistory) {
        // The marker stays with the history.
        if (somethingAt(folder.markerPath()) && !report.kept.contains(shown(folder.markerPath())))
            report.kept.append(shown(folder.markerPath()));
    } else if (report.ok()) {
        if (QDir().rmdir(folder.path()))
            report.removed.append(shown(folder.path()));
        else
            report.failed.append(QStringLiteral("%1: the folder cannot be removed").arg(shown(folder.path())));
    }
    // The known paths belong to the app's default work folder only (its
    // browsers make them); a folder anywhere else has none.
    const bool defaultFolder = QDir::cleanPath(folder.path()) == QDir::cleanPath(WorkFolder::defaultPath(folder.appIdentifier()));
    for (const QString &path : defaultFolder ? knownPaths(folder.appIdentifier()) : QStringList()) {
        if (!somethingAt(path))
            continue;
        if (removeEntry(path, &report.failed))
            report.removed.append(shown(path));
    }
    return report;
}

QString Uninstall::removeProgram(const QString &packageName, const QString &linuxPackageBinDir, bool *removed)
{
    *removed = false;
    const QString binDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
    Q_UNUSED(packageName);
    Q_UNUSED(linuxPackageBinDir);
    const QString bundle = QDir::cleanPath(binDir + QStringLiteral("/../.."));
    if (!bundle.endsWith(QStringLiteral(".app")) || isLink(bundle))
        return QStringLiteral("The app does not run from an app bundle, so nothing was moved to the Trash.");
    QFile file(bundle);
    if (file.moveToTrash()) {
        *removed = true;
        return QStringLiteral("The app was moved to the Trash.");
    }
    return QStringLiteral("The app could not be moved to the Trash from where it runs (%1). Drag %2 from your "
                          "Applications folder to the Trash to finish.")
        .arg(shown(bundle), QFileInfo(bundle).fileName());
#elif defined(Q_OS_LINUX)
    const QString pkexec = QStringLiteral("/usr/bin/pkexec");
    const QString command = QStringLiteral("sudo apt remove %1").arg(packageName);
    if (linuxPackageBinDir.isEmpty() || binDir != QDir::cleanPath(linuxPackageBinDir))
        return QStringLiteral("The app does not run from its installed package, so no package was removed.");
    if (!QFileInfo(pkexec).isExecutable())
        return QStringLiteral("To remove the program, run: %1").arg(command);
    const int code = QProcess::execute(pkexec, {QStringLiteral("/usr/bin/dpkg"), QStringLiteral("--remove"), packageName});
    if (code == 0) {
        *removed = true;
        return QStringLiteral("The program was removed.");
    }
    return QStringLiteral("The program was not removed (pkexec ended with %1). To remove it, run: %2").arg(code).arg(command);
#else
    Q_UNUSED(packageName);
    Q_UNUSED(linuxPackageBinDir);
    Q_UNUSED(binDir);
    return QStringLiteral("The program is removed by its uninstaller.");
#endif
}

} // namespace app
