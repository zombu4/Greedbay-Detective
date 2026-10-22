#include "core/SafeDelete.h"

#include "core/PathChecks.h"
#include "core/Version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

namespace app {

DeleteGrant DeleteGrants::confirmedUninstall() { return DeleteGrant(DeleteGrant::Scope::Everything); }

DeleteGrant DeleteGrants::confirmedSignOut() { return DeleteGrant(DeleteGrant::Scope::BrowserProfiles); }

namespace SafeDelete {

namespace {

// Constructed on first use: tests register roots before main() runs
// (tests/support/TestGuard.cpp), when other globals may not exist yet.
QMutex &forbiddenMutex()
{
    static QMutex mutex;
    return mutex;
}

QStringList &forbiddenList()
{
    static QStringList list;
    return list;
}

QString shown(const QString &path) { return QDir::toNativeSeparators(path); }

// <GenericDataLocation>/<name>, with Qt's test mode off for the moment of
// asking (a test must still know the real place, never write there).
QString dataFolder(const QString &name)
{
    const bool testMode = QStandardPaths::isTestModeEnabled();
    if (testMode)
        QStandardPaths::setTestModeEnabled(false);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (testMode)
        QStandardPaths::setTestModeEnabled(true);
    return base.isEmpty() ? QString() : QDir::cleanPath(base + QLatin1Char('/') + name);
}

bool removeEntry(const QString &path, QStringList *failed)
{
    const QFileInfo info(path);
    if (isLink(path)) {
#if defined(Q_OS_WIN)
        // A junction or a link to a folder is a folder entry of its own:
        // removing it like an empty folder leaves its target alone.
        const bool removed = (info.isJunction() || info.isDir()) ? QDir().rmdir(path) : QFile::remove(path);
#else
        // Every link is a file entry; unlinking it leaves its target alone.
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
    if (!info.exists())
        return true;
    if (!QFile::remove(path)) {
        failed->append(QStringLiteral("%1: the file cannot be removed").arg(shown(path)));
        return false;
    }
    return true;
}

} // namespace

QString realWorkFolder() { return dataFolder(QString::fromLatin1(version::kIdentifier)); }

QString devWorkFolder() { return dataFolder(QString::fromLatin1(version::kIdentifier) + QStringLiteral("-Dev")); }

bool isInside(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty())
        return false;
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanRoot = QDir::cleanPath(root);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Case-insensitive file systems by default.
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return cleanPath.compare(cleanRoot, sensitivity) == 0 || cleanPath.startsWith(cleanRoot + QLatin1Char('/'), sensitivity);
}

void forbid(const QString &root)
{
    if (root.isEmpty())
        return;
    const QMutexLocker locker(&forbiddenMutex());
    const QString clean = QDir::cleanPath(root);
    if (!forbiddenList().contains(clean))
        forbiddenList().append(clean);
}

QStringList forbiddenRoots()
{
    const QMutexLocker locker(&forbiddenMutex());
    return forbiddenList();
}

QString refusal(const QString &path, const DeleteGrant *grant) { return refusalFor(path, grant, realWorkFolder()); }

QString refusalFor(const QString &path, const DeleteGrant *grant, const QString &realRoot)
{
    if (path.isEmpty() || QDir::isRelativePath(path))
        return QStringLiteral("%1: not an absolute path; nothing was deleted").arg(shown(path));
    for (const QString &root : forbiddenRoots()) {
        if (isInside(path, root))
            return QStringLiteral("%1: inside a protected folder; nothing was deleted").arg(shown(path));
    }
    const QString real = realRoot;
    if (!isInside(path, real))
        return {};
    if (!grant)
        return QStringLiteral("%1: inside the app's work folder, and only the confirmed Uninstall may delete there; nothing "
                              "was deleted")
            .arg(shown(path));
    if (grant->scope() == DeleteGrant::Scope::Everything)
        return {};
    if (isInside(path, real + QStringLiteral("/browser")) && QDir::cleanPath(path) != QDir::cleanPath(real + QStringLiteral("/browser")))
        return {};
    return QStringLiteral("%1: outside the browser profiles; nothing was deleted").arg(shown(path));
}

bool removeTree(const QString &path, const DeleteGrant *grant, QStringList *failed)
{
    const QString why = refusal(path, grant);
    if (!why.isEmpty()) {
        failed->append(why);
        return false;
    }
    return removeEntry(QDir::cleanPath(path), failed);
}

} // namespace SafeDelete

} // namespace app
