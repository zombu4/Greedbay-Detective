#pragma once

// Process listing for tests: the descendants of a process (children,
// grandchildren, ...), whether a process still runs, and its name.
// macOS: libproc. Linux: /proc. Windows: a Toolhelp snapshot.
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QMap>
#include <QString>

#if defined(Q_OS_MACOS)
#  include <libproc.h>
#  include <sys/proc_info.h>
#  include <cerrno>
#  include <csignal>
#  include <vector>
#elif defined(Q_OS_LINUX)
#  include <QDir>
#elif defined(Q_OS_WIN)
#  include <qt_windows.h>
#  include <tlhelp32.h>
#endif

struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;
};

// Every running process with its parent.
inline QList<ProcessInfo> allProcesses()
{
    QList<ProcessInfo> result;
#if defined(Q_OS_MACOS)
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0)
        return result;
    std::vector<pid_t> pids(static_cast<size_t>(count) + 64);
    const int filled = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    for (int i = 0; i < filled; ++i) {
        proc_bsdinfo info{};
        if (proc_pidinfo(pids[static_cast<size_t>(i)], PROC_PIDTBSDINFO, 0, &info, sizeof info) != sizeof info)
            continue;
        // The executable's file name: pbi_name can be a display name a
        // process sets for itself, the path cannot.
        char path[PROC_PIDPATHINFO_MAXSIZE];
        const int length = proc_pidpath(pids[static_cast<size_t>(i)], path, sizeof path);
        const QString name = length > 0 ? QString::fromLocal8Bit(path, length).section(QLatin1Char('/'), -1)
                                        : QString::fromLocal8Bit(info.pbi_comm);
        result.append({static_cast<qint64>(pids[static_cast<size_t>(i)]), static_cast<qint64>(info.pbi_ppid), name});
    }
#elif defined(Q_OS_LINUX)
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isNumber = false;
        const qint64 pid = entry.toLongLong(&isNumber);
        if (!isNumber)
            continue;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        // "pid (comm) state ppid ...": comm may contain spaces and ')'.
        const QByteArray line = stat.readAll();
        const qsizetype open = line.indexOf('(');
        const qsizetype close = line.lastIndexOf(')');
        if (open < 0 || close < open)
            continue;
        const QList<QByteArray> fields = line.mid(close + 2).split(' ');
        if (fields.size() < 2 || fields.at(0) == "Z")
            continue;
        result.append({pid, fields.at(1).toLongLong(), QString::fromLocal8Bit(line.mid(open + 1, close - open - 1))});
    }
#elif defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        result.append({static_cast<qint64>(entry.th32ProcessID), static_cast<qint64>(entry.th32ParentProcessID),
                       QString::fromWCharArray(entry.szExeFile)});
    CloseHandle(snapshot);
#endif
    return result;
}

// The descendants of `root` that run now.
inline QList<ProcessInfo> descendants(qint64 root)
{
    const QList<ProcessInfo> processes = allProcesses();
    QMultiMap<qint64, ProcessInfo> byParent;
    for (const ProcessInfo &process : processes) {
        if (process.pid != process.parentPid)
            byParent.insert(process.parentPid, process);
    }
    QList<ProcessInfo> result;
    QList<qint64> queue{root};
    while (!queue.isEmpty()) {
        const qint64 parent = queue.takeFirst();
        for (const ProcessInfo &child : byParent.values(parent)) {
            result.append(child);
            queue.append(child.pid);
        }
    }
    return result;
}

// True while `pid` runs (a process that has exited but was not yet reaped
// counts as gone on Linux and Windows; on macOS kill() still sees it).
inline bool processRunning(qint64 pid)
{
#if defined(Q_OS_MACOS)
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#elif defined(Q_OS_LINUX)
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return false;
    const QByteArray line = stat.readAll();
    const qsizetype close = line.lastIndexOf(')');
    return close >= 0 && line.mid(close + 2, 1) != "Z";
#elif defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    Q_UNUSED(pid);
    return false;
#endif
}
