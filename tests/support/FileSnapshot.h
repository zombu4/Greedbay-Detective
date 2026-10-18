#pragma once

// A list of every file and folder under some roots (size and modification
// time per file), and the difference between two such lists. Links are
// listed as entries, never followed.
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>

struct SnapshotEntry {
    bool isDir = false;
    qint64 size = 0;
    qint64 modifiedMs = 0;
};

struct FileSnapshot {
    QHash<QString, SnapshotEntry> entries;
    int unreadableDirs = 0;
};

// `path` itself or anything inside it.
inline bool isUnder(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/'));
}

// The canonical form of an existing path (links in it resolved), so that
// paths from different sources compare equal; an empty string when the path
// does not exist.
inline QString canonical(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

// Removes roots that do not exist and roots inside another root.
inline QStringList distinctRoots(const QStringList &roots)
{
    QStringList existing;
    for (const QString &root : roots) {
        const QString path = canonical(root);
        if (!path.isEmpty() && !existing.contains(path))
            existing.append(path);
    }
    QStringList result;
    for (const QString &root : existing) {
        const bool nested = std::any_of(existing.cbegin(), existing.cend(), [&root](const QString &other) {
            return other != root && isUnder(root, other);
        });
        if (!nested)
            result.append(root);
    }
    return result;
}

inline FileSnapshot takeSnapshot(const QStringList &roots, const QStringList &excluded)
{
    FileSnapshot snapshot;
    for (const QString &root : roots) {
        QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            const QString path = info.filePath();
            if (std::any_of(excluded.cbegin(), excluded.cend(),
                            [&path](const QString &folder) { return isUnder(path, folder); }))
                continue;
            SnapshotEntry entry;
            entry.isDir = info.isDir() && !info.isSymLink();
            if (!entry.isDir) {
                entry.size = info.isSymLink() ? 0 : info.size();
                entry.modifiedMs = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
            } else if (!info.isReadable()) {
                ++snapshot.unreadableDirs;
            }
            snapshot.entries.insert(path, entry);
        }
    }
    return snapshot;
}

// "added: <path>", "removed: <path>" and "changed: <path>" lines, sorted.
// A folder only counts when it was added or removed (its modification time
// changes whenever something inside changes).
inline QStringList snapshotDifference(const FileSnapshot &before, const FileSnapshot &after)
{
    QStringList lines;
    for (auto it = after.entries.cbegin(); it != after.entries.cend(); ++it) {
        const auto old = before.entries.constFind(it.key());
        if (old == before.entries.cend()) {
            lines << QStringLiteral("added: ") + it.key();
        } else if (!it->isDir && (old->isDir || old->size != it->size || old->modifiedMs != it->modifiedMs)) {
            lines << QStringLiteral("changed: ") + it.key();
        }
    }
    for (auto it = before.entries.cbegin(); it != before.entries.cend(); ++it) {
        if (!after.entries.contains(it.key()))
            lines << QStringLiteral("removed: ") + it.key();
    }
    lines.sort();
    return lines;
}
