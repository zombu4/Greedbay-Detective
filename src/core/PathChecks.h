#pragma once

#include <QFileInfo>
#include <QString>

namespace app {

// True for anything that redirects to another location: symbolic links,
// Windows junctions and macOS Finder aliases. Code that deletes or creates
// files never follows these.
inline bool isLink(const QString &path)
{
    const QFileInfo info(path);
    return info.isSymLink() || info.isJunction() || info.isAlias();
}

// True for an existing regular file that is not a link of any kind.
inline bool isPlainFile(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && !isLink(path);
}

// True when anything exists at `path`, including a link whose target is gone.
inline bool somethingAt(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

} // namespace app
