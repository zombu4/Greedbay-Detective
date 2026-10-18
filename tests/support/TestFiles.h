#pragma once

// File helpers for tests. They return a result instead of using QVERIFY, so a
// failed setup step stops the calling test (QVERIFY inside a helper would only
// return from the helper).
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>

inline bool writeTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
}

inline QByteArray readTestFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Creates `link` pointing at the existing directory `target`: a symbolic link
// on Unix, a junction on Windows (junctions need no extra privileges).
inline bool makeDirectoryLink(const QString &target, const QString &link)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("cmd.exe"),
                  {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                   QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit
           && process.exitCode() == 0;
#else
    return QFile::link(target, link);
#endif
}

// True where tests can create a link to a single file (symbolic links on
// Unix; on Windows file links need extra privileges, junctions are folders only).
inline constexpr bool kCanLinkFiles =
#ifdef Q_OS_WIN
    false;
#else
    true;
#endif

// After a folder was made read-only: true when this process can still create
// files in it (for example when running as root), so a test that relies on
// the folder being read-only must be skipped.
inline bool permissionsIgnored(const QString &readOnlyDir)
{
    QFile probe(readOnlyDir + QStringLiteral("/.permission-probe"));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}
