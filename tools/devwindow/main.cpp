// Developer window entry point. Uses the app's real work folder (default
// location) and its single-instance lock, like the app will.
#include "DevWindow.h"

#include "core/Logging.h"
#include "core/SingleInstance.h"
#include "core/Version.h"
#include "core/WorkFolder.h"

#include <QApplication>
#include <QMessageBox>

#include <optional>

int main(int argc, char *argv[])
{
    const QString identifier = QString::fromLatin1(app::version::kIdentifier);
    QString error;
    const std::optional<app::WorkFolder> folder =
        app::WorkFolder::open(app::WorkFolder::defaultPath(identifier), identifier, &error);
    const bool tempRedirected = folder && folder->redirectTempDirectory(&error);

    // No shader binaries in the per-user cache folder (outside the work
    // folder): "disable all disk-based caching of shaders" (Qt docs).
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(identifier);
    if (!folder || !tempRedirected) {
        QMessageBox::critical(nullptr, QString::fromLatin1(app::version::kDisplayName),
                              QStringLiteral("The work folder cannot be used: %1").arg(error));
        return 1;
    }
    app::SingleInstance instance(folder->lockPath());
    if (!instance.tryAcquire()) {
        QMessageBox::critical(nullptr, QString::fromLatin1(app::version::kDisplayName), instance.error());
        return 1;
    }
    if (!app::Logging::install(folder->logsDir(), identifier, &error)) {
        QMessageBox::critical(nullptr, QString::fromLatin1(app::version::kDisplayName), error);
        return 1;
    }

    int code = 0;
    {
        // Destroyed before logging stops, so its last messages are logged.
        DevWindow window(*folder);
        window.show();
        code = QApplication::exec();
    }
    app::Logging::uninstall();
    return code;
}
