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
#include "core/WorkFolder.h"

#include <QApplication>
#include <QFileInfo>
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

    // The worker never creates a work folder; the app has created it.
    if (!QFileInfo(options->workFolder).isDir()) {
        printError(QStringLiteral("the work folder does not exist"));
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

    // No GPU acceleration (Chromium switch --disable-gpu, "Disables GPU
    // hardware acceleration"): Chromium's GPU path writes shader caches
    // outside the work folder (seen on macOS in the per-user cache folder,
    // com.apple.metal/), and reading pages or showing the sign-in page does
    // not need it. Added to any flags already set in the environment.
    QByteArray chromiumFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!chromiumFlags.split(' ').contains("--disable-gpu")) {
        chromiumFlags += chromiumFlags.isEmpty() ? "--disable-gpu" : " --disable-gpu";
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
    }

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

    app::browser::WorkerHost host(*folder, sessionOptions);
    if (!host.connectToApp(options->server, options->token, &error)) {
        printError(error);
        app::Logging::uninstall();
        return kExitStartFailed;
    }
    const int code = QApplication::exec();
    app::Logging::uninstall();
    return code;
}
