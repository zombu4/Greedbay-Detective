// The browser worker: a separate process that runs the Qt WebEngine browser
// for one session and then exits. The app starts it (core/BrowserWorker) with
//   --work-folder <path> --server <local socket name> --token <random token>
// and talks to it over the local socket (core/BrowserProtocol.h).
//
// Exit codes: 0 after "quit", 2 when it cannot start (bad arguments, work
// folder missing or refused, no connection to the app), 3 when the
// connection to the app was lost (WorkerHost::kExitConnectionLost).
#include "browser/BrowserSession.h"
#include "browser/WorkerHost.h"
#include "core/Logging.h"
#include "core/Version.h"
#include "core/WebEngineData.h"
#include "core/WorkFolder.h"

#include <QApplication>
#include <QFileInfo>
#include <QLockFile>
#include <QStringList>

#include <cstdio>
#include <optional>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <shellapi.h>
#endif

namespace {

constexpr int kExitStartFailed = 2;

// The command line as Unicode strings. On Windows argv is in the local
// code page and can lose characters of a path, so the wide command line is
// read instead.
QStringList commandLine(int argc, char *argv[])
{
    QStringList arguments;
#ifdef Q_OS_WIN
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    int count = 0;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide) {
        for (int i = 0; i < count; ++i)
            arguments << QString::fromWCharArray(wide[i]);
        LocalFree(wide);
    }
#else
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
#endif
    return arguments;
}

struct Options {
    QString workFolder;
    QString server;
    QString token;
};

std::optional<Options> parse(const QStringList &arguments)
{
    Options options;
    for (qsizetype i = 1; i + 1 < arguments.size(); i += 2) {
        const QString &name = arguments.at(i);
        const QString &value = arguments.at(i + 1);
        if (name == QLatin1String("--work-folder"))
            options.workFolder = value;
        else if (name == QLatin1String("--server"))
            options.server = value;
        else if (name == QLatin1String("--token"))
            options.token = value;
        else
            return std::nullopt;
    }
    if (arguments.size() % 2 != 1 || options.workFolder.isEmpty() || options.server.isEmpty()
        || options.token.isEmpty())
        return std::nullopt;
    return options;
}

void printError(const QString &message)
{
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
}

} // namespace

int main(int argc, char *argv[])
{
    const std::optional<Options> options = parse(commandLine(argc, argv));
    if (!options) {
        printError(QStringLiteral("usage: --work-folder <path> --server <name> --token <token>"));
        return kExitStartFailed;
    }

    // The worker only uses a work folder the app has created: the folder and
    // its marker must exist (WorkFolder::open below only recreates missing
    // subfolders of it).
    if (!QFileInfo(options->workFolder).isDir()
        || !QFileInfo::exists(options->workFolder + QLatin1Char('/') + QLatin1String(app::WorkFolder::kMarkerFileName))) {
        printError(QStringLiteral("the work folder does not exist or was not created by the app"));
        return kExitStartFailed;
    }
    const QString identifier = QString::fromLatin1(app::version::kIdentifier);
    QString error;
    const std::optional<app::WorkFolder> folder = app::WorkFolder::open(options->workFolder, identifier, &error);
    if (!folder) {
        printError(error);
        return kExitStartFailed;
    }
    // Before QApplication, so that Qt and Chromium use the work folder's tmp/
    // from the start.
    if (!folder->redirectTempDirectory(&error)) {
        printError(error);
        return kExitStartFailed;
    }

    // The browser session lock, taken by the session on first use and
    // released when this object is destroyed: after QApplication (declared
    // below) has shut Qt WebEngine down, so sign-out can never run while
    // Chromium may still write the profile. No age limit (see
    // WebEngineData::clear).
    QLockFile sessionLock(app::WebEngineData::sessionLockPath(*folder));
    sessionLock.setStaleLockTime(0);

    // No GPU acceleration (Chromium switch --disable-gpu, "Disables GPU
    // hardware acceleration"): Chromium's GPU path writes shader caches
    // outside the work folder (seen on macOS in the per-user cache folder,
    // com.apple.metal/), and reading pages or showing the sign-in page does
    // not need it. Added to any flags already set in the environment.
    // The GPU shader disk cache is switched off too (Chromium switch
    // --disable-gpu-shader-disk-cache, "Disables the GPU shader on disk
    // cache"), so no shader cache is written even if a GPU path runs.
    QByteArray chromiumFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    for (const QByteArray &flag : {QByteArray("--disable-gpu"), QByteArray("--disable-gpu-shader-disk-cache")}) {
        if (!chromiumFlags.split(' ').contains(flag))
            chromiumFlags += chromiumFlags.isEmpty() ? flag : ' ' + flag;
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);

    // Required by Qt WebEngine before the application object exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(identifier);
    // Hiding or closing the browser window must not end the worker; only
    // "quit" or a lost connection does.
    QApplication::setQuitOnLastWindowClosed(false);

    if (!app::Logging::install(folder->logsDir(), identifier + QStringLiteral("Browser"), &error)) {
        printError(error);
        return kExitStartFailed;
    }

    app::browser::SessionOptions sessionOptions;
#ifdef APP_BROWSER_ALLOW_LOCAL_TEST_PAGES
    // Test build only (target browserworker_test): local fixture pages.
    sessionOptions.allowLocalTestPages = true;
#endif

    app::browser::WorkerHost host(*folder, sessionOptions, &sessionLock);
    if (!host.connectToApp(options->server, options->token, &error)) {
        printError(error);
        app::Logging::uninstall();
        return kExitStartFailed;
    }
    const int code = QApplication::exec();
    app::Logging::uninstall();
    return code;
}
