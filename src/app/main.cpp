// The app's entry point: the work folder (default location), the
// single-instance lock, logging, the database (schema upgrades with backup),
// then the Qt Quick user interface (src/ui/qml, logic in src/ui). After the
// user chose "Uninstall..." (macOS, Linux), the data and the program are
// removed once all of that is closed (core/Uninstall).
//
// "--plugin-runner" as the only argument: no window; runs one plugin request
// from standard input and exits (src/plugins/PluginRunner.h: plugin code and
// plugin .zip files are only handled in such a separate process).
//
// "--model-runner" as the only argument: no window; the local model's
// runner (src/model/Runner.h): requests on standard input, answers on
// standard output, until the input ends.
//
// "--uninstall-data=keep" or "--uninstall-data=delete" as the only argument:
// no window; removes the user's data the same way and exits (the Windows
// uninstaller calls it, packaging/windows/NSIS.template.in). Exit codes: 0
// done (or nothing saved), 2 refused (the app runs, or the folder is not
// the app's), 3 some of it could not be removed.
#include "core/Logging.h"
#include "core/PathChecks.h"
#include "core/SingleInstance.h"
#include "core/Backups.h"
#include "core/Uninstall.h"
#include "core/Version.h"
#include "core/WorkFolder.h"
#include "db/Database.h"
#include "db/Migrations.h"
#include "model/Runner.h"
#include "plugins/PluginRunner.h"
#include "ui/Backend.h"
#include "ui/Fonts.h"
#include "ui/PhotoProvider.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <cstring>
#include <optional>

Q_IMPORT_QML_PLUGIN(AppUiPlugin)

namespace {

const char kModule[] = "GreedbayDetective.Ui";
const char kConnection[] = "app";

// Shows `message` in a small window until the user closes it.
int showStartupError(QQmlApplicationEngine &engine, const QString &message)
{
    engine.setInitialProperties({{QStringLiteral("message"), message}});
    engine.loadFromModule(kModule, "StartupError");
    if (engine.rootObjects().isEmpty())
        return 1;
    QGuiApplication::exec();
    return 1;
}

QString databaseProblem(const app::DatabaseResult &result, const app::WorkFolder &folder)
{
    switch (result.outcome) {
    case app::DatabaseOutcome::RefusedNewer:
        return QStringLiteral("The saved lookups were written by a newer version of the app. Install that version "
                              "again; nothing was changed. (%1)").arg(result.error);
    case app::DatabaseOutcome::NotOurs:
        return QStringLiteral("The database file in the work folder was not made by this app, so it was not opened "
                              "or changed: %1").arg(result.error);
    case app::DatabaseOutcome::MissingWithBackups:
        return QStringLiteral("The database file is missing, but backups of it exist in %1. Nothing was created, so "
                              "no history is replaced: copy the newest backup back as the database file to restore "
                              "it. (%2)")
            .arg(QDir::toNativeSeparators(folder.backupsDir()), result.error);
    default:
        return QStringLiteral("The database cannot be opened: %1").arg(result.error);
    }
}

// What an uninstall removed, kept and could not remove, in plain words.
QString reportText(const app::Uninstall::Report &report)
{
    QStringList lines;
    if (!report.removed.isEmpty())
        lines << QStringLiteral("Removed:") << report.removed;
    if (!report.kept.isEmpty())
        lines << QString() << QStringLiteral("Kept (your saved lookups and history):") << report.kept;
    if (!report.failed.isEmpty())
        lines << QString() << QStringLiteral("Could not be removed:") << report.failed;
    if (lines.isEmpty())
        lines << QStringLiteral("There was nothing to remove.");
    return lines.join(QLatin1Char('\n'));
}

// The user's data, without a window (--uninstall-data). Never creates a
// work folder that is not there.
int uninstallData(const QString &identifier, bool keepHistory)
{
    const QString path = app::WorkFolder::defaultPath(identifier);
    if (path.isEmpty() || !app::somethingAt(path))
        return 0;
    QString error;
    const std::optional<app::WorkFolder> folder = app::WorkFolder::open(path, identifier, &error);
    const QString refusal = folder ? app::Uninstall::check(*folder) : error;
    if (!refusal.isEmpty()) {
        std::fprintf(stderr, "%s\n", qPrintable(refusal));
        return 2;
    }
    // A copy of the database outside the work folder first; the Windows
    // uninstaller has no backups choice, so they stay (core/Backups).
    app::Backups backups(app::Backups::folderFor(identifier));
    if (!backups.beforeChange(folder->databasePath(), QStringLiteral("uninstall"), &error))
        std::fprintf(stderr, "backups: no copy before the uninstall: %s\n", qPrintable(error));
    // The Windows uninstaller starts this after its own dialog asked.
    const app::Uninstall::Report report = app::Uninstall::run(*folder, keepHistory, app::DeleteGrants::confirmedUninstall());
    std::fprintf(stdout, "%s\n", qPrintable(reportText(report)));
    return report.ok() ? 0 : 3;
}

// The end of an in-app uninstall (macOS, Linux): the data, then the program,
// then a window that says what was done.
int finishUninstall(const app::WorkFolder &folder, bool keepHistory, bool deleteBackups, const app::DeleteGrant &grant)
{
    QString heading = QStringLiteral("Greedbay Detective was uninstalled");
    QString text;
    // A copy of the database outside the work folder first (core/Backups).
    QString backupError;
    app::Backups backups(app::Backups::folderFor(folder.appIdentifier()));
    if (!deleteBackups && !backups.beforeChange(folder.databasePath(), QStringLiteral("uninstall"), &backupError))
        qWarning().noquote() << "backups: no copy before the uninstall:" << backupError;
    const QString refusal = app::Uninstall::check(folder);
    if (!refusal.isEmpty()) {
        heading = QStringLiteral("Nothing was uninstalled");
        text = refusal;
    } else {
        app::Uninstall::Report report = app::Uninstall::run(folder, keepHistory, grant);
        // The backups only when the user ticked that too.
        if (deleteBackups && app::SafeDelete::removeTree(backups.folder(), &grant, &report.failed))
            report.removed.append(QDir::toNativeSeparators(backups.folder()));
        text = reportText(report);
        if (report.ok()) {
            bool removed = false;
            text += QStringLiteral("\n\n") + app::Uninstall::removeProgram(QString::fromLatin1(app::version::kPackageName),
                                                                           QStringLiteral(APP_LINUX_PACKAGE_BIN_DIR), &removed);
        } else {
            heading = QStringLiteral("The uninstall did not finish");
            text += QStringLiteral("\n\nThe program was left installed, because not all of your data could be removed.");
        }
    }
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("message"), text}, {QStringLiteral("heading"), heading}});
    engine.loadFromModule(kModule, "StartupError");
    if (engine.rootObjects().isEmpty())
        return 1;
    QGuiApplication::exec();
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // The data's identifier (owner decision 2026-10-22): only Release builds
    // (the installers') use the real work folder; every other build uses
    // "<identifier>-Dev" (src/CMakeLists.txt defines APP_REAL_DATA).
#if defined(APP_REAL_DATA)
    const QString identifier = QString::fromLatin1(app::version::kIdentifier);
#else
    const QString identifier = QString::fromLatin1(app::version::kIdentifier) + QStringLiteral("-Dev");
#endif
    if (argc == 2 && std::strcmp(argv[1], "--plugin-runner") == 0) {
        QCoreApplication runner(argc, argv);
        return app::plugins::runPluginRequestOnStdio();
    }
    if (argc == 2 && std::strcmp(argv[1], "--model-runner") == 0)
        return app::model::runModelRunner();
    if (argc == 2 && (std::strcmp(argv[1], "--uninstall-data=keep") == 0 || std::strcmp(argv[1], "--uninstall-data=delete") == 0))
        return uninstallData(identifier, std::strcmp(argv[1], "--uninstall-data=keep") == 0);
    QString error;
    const std::optional<app::WorkFolder> folder =
        app::WorkFolder::open(app::WorkFolder::defaultPath(identifier), identifier, &error);
    const bool tempRedirected = folder && folder->redirectTempDirectory(&error);

    // Nothing outside the work folder (work-cycle rule 8):
    // - no shader binaries and no Qt Quick pipeline cache in the per-user
    //   cache folder: AA_DisableShaderDiskCache "Disables caching of shader
    //   program binaries on disk" (Qt docs) and turns the automatic pipeline
    //   cache off (qtdeclarative 6.12.0 qquickgraphicsconfiguration.cpp);
    // - no QML cache files (<cache folder>/qmlcache, Qt docs "The QML Disk
    //   Cache"): QML_DISK_CACHE=aot uses only the code compiled into the app
    //   and neither reads nor writes .qmlc files; QML_FORCE_DISK_CACHE would
    //   override it, so it is removed (qtdeclarative 6.12.0 qv4engine.cpp,
    //   ExecutionEngine::diskCacheOptions).
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    qputenv("QML_DISK_CACHE", "aot");
    qunsetenv("QML_FORCE_DISK_CACHE");

    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(identifier);
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(app::version::kDisplayName));
    // The Basic style, the same on every system; the app draws its own look
    // on top of it (src/ui/qml).
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;

    if (!folder || !tempRedirected)
        return showStartupError(engine, QStringLiteral("The work folder cannot be used: %1").arg(error));
    app::SingleInstance instance(folder->lockPath());
    if (!instance.tryAcquire())
        return showStartupError(engine, instance.error());
    if (!app::Logging::install(folder->logsDir(), identifier, &error))
        return showStartupError(engine, error);

    int code = 1;
    std::optional<bool> uninstallKeep;
    std::optional<app::DeleteGrant> uninstallGrant;
    bool uninstallBackups = false;
    {
        if (!app::ui::loadFonts(&error))
            qWarning().noquote() << "ui:" << error << "- the system font is used instead";
        const std::optional<QList<app::Migration>> migrations = app::loadMigrations(QStringLiteral(":/migrations"), &error);
        if (!migrations) {
            code = showStartupError(engine, QStringLiteral("The database upgrades cannot be read: %1").arg(error));
        } else {
            // Backups outside the work folder (core/Backups): a restore the
            // user chose runs first, then a copy before any upgrade.
            app::Backups backups(app::Backups::folderFor(identifier));
            if (!backups.applyPendingRestore(folder->databasePath(), &error))
                qWarning().noquote() << "backups: the restore did not run:" << error;
            const int latest = migrations->isEmpty() ? 0 : migrations->last().version;
            if (!backups.beforeMigration(folder->databasePath(), latest, &error))
                qWarning().noquote() << "backups: no copy before the upgrade:" << error;
            const app::DatabaseResult database = app::Database::open(QString::fromLatin1(kConnection), folder->databasePath(),
                                                                      folder->backupsDir(), identifier, *migrations);
            if (!database.ok()) {
                code = showStartupError(engine, databaseProblem(database, *folder));
            } else {
                if (database.outcome == app::DatabaseOutcome::Upgraded)
                    qInfo().noquote() << "database upgraded from version" << database.fromVersion << "to"
                                      << database.toVersion << "- backup" << database.backupPath;
                if (!database.warning.isEmpty())
                    qWarning().noquote() << "database:" << database.warning;
                if (!backups.daily(QString::fromLatin1(kConnection), &error))
                    qWarning().noquote() << "backups: no daily copy:" << error;
                {
                    app::ui::Backend backend(*folder, QString::fromLatin1(kConnection));
                    if (!backend.fatalError().isEmpty()) {
                        code = showStartupError(engine, backend.fatalError());
                    } else {
                        // Declared after the backend, so the views go before
                        // the objects they show.
                        QQmlApplicationEngine ui;
                        ui.addImageProvider(QStringLiteral("listing"), backend.takePhotoProvider());
                        ui.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)}});
                        ui.loadFromModule(kModule, "Main");
                        code = ui.rootObjects().isEmpty() ? 1 : QGuiApplication::exec();
                    }
                    uninstallKeep = backend.uninstallRequest();
                    uninstallGrant = backend.uninstallGrant();
                    uninstallBackups = backend.uninstallDeletesBackups();
                }
                app::Database::close(QString::fromLatin1(kConnection));
            }
        }
    }
    app::Logging::uninstall();
    if (uninstallKeep && uninstallGrant) {
        // Everything in the work folder is closed now: the views, the
        // database, the log; the lock goes last.
        instance.release();
        return finishUninstall(*folder, *uninstallKeep, uninstallBackups, *uninstallGrant);
    }
    return code;
}
