#pragma once

#include <QString>

#include <optional>

namespace app {

// The single folder that holds everything the app writes: the database and
// its backups, logs, settings, the browser profile and temporary files.
//
// Layout (all paths relative to path()):
//   .workfolder              marker holding a random ID; proves the app created the folder
//   .lock                    single-instance lock while the app runs
//   <appIdentifier>.sqlite   database
//   backups/                 database copies made before each schema upgrade
//   logs/                    rotating log files
//   settings.ini             settings in INI format
//   page-count.json          eBay pages loaded today (core/PageBudget, the daily cap)
//   browser/                 profiles of the installed browsers the app drives,
//                            one subfolder per browser (browser/<id>/), plus
//                            their lock files (browser/<id>.lock)
//   tmp/                     temporary files of this process
class WorkFolder {
public:
    // The default location: <GenericDataLocation>/<appIdentifier>, which is
    //   macOS    ~/Library/Application Support/<appIdentifier>
    //   Windows  %LOCALAPPDATA%\<appIdentifier>
    //   Linux    ~/.local/share/<appIdentifier>
    // It is built from GenericDataLocation rather than AppLocalDataLocation so
    // that an organization name set on the application object can never become
    // part of the path. Returns an empty string when the platform reports no
    // writable data location. Safe to call before QCoreApplication exists.
    static QString defaultPath(const QString &appIdentifier);

    // Opens the work folder at `path`, creating it with its subfolders and the
    // marker on first use.
    //
    // Refuses (returns std::nullopt and sets *error) when:
    //   - `path`, one of its subfolders, settings.ini, page-count.json, the
    //     database file or the lock file is a symbolic link, junction or alias,
    //   - the marker exists but does not hold a well-formed ID,
    //   - the folder exists, has no marker and is not empty (the app never
    //     adopts a folder it did not create),
    //   - a folder or the marker cannot be created.
    // On Unix the folder is restricted to its owner (mode 0700), because it
    // holds the browser's cookies.
    static std::optional<WorkFolder> open(const QString &path, const QString &appIdentifier,
                                          QString *error);

    // True when `id` is a UUID in the exact form the marker uses.
    static bool isValidMarkerId(const QString &id);

    // Points this process's temporary directory at tmpDir(): TMPDIR on Unix,
    // TEMP and TMP on Windows. Call before QCoreApplication is constructed so
    // that Qt and the libraries it loads use it from the start.
    bool redirectTempDirectory(QString *error) const;

    QString path() const { return m_path; }
    QString appIdentifier() const { return m_appIdentifier; }
    QString markerId() const { return m_markerId; }

    QString markerPath() const;
    QString lockPath() const;
    QString databasePath() const;
    QString backupsDir() const;
    QString logsDir() const;
    QString settingsPath() const;
    QString pageCountPath() const;
    QString browserProfilesDir() const;
    QString tmpDir() const;

    static constexpr const char *kMarkerFileName = ".workfolder";
    static constexpr const char *kLockFileName = ".lock";

private:
    WorkFolder(QString path, QString appIdentifier, QString markerId);

    QString m_path;
    QString m_appIdentifier;
    QString m_markerId;
};

} // namespace app
