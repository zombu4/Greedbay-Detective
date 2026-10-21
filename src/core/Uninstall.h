#pragma once

#include "core/WorkFolder.h"

#include <QString>
#include <QStringList>

namespace app {

// Full uninstall of the user's data (PLAN 9, 9.1a; owner decision: full
// uninstall with a prompt to keep saved history). Nothing is searched for
// (work cycle rule 8): only the work folder and the known paths below.
//
// "Keep saved lookups and history" keeps keptNames() in the work folder -
// the database (and its rollback journal), its backups, the marker and the
// brands and models the user confirmed - and deletes everything else there (logs, settings, the
// browser profiles with their cookies, the daily page count, temporary
// files, installed plugins, the shared seller list's copy and this
// installation's report key, the downloaded local models). "Delete everything" deletes the whole work
// folder. A later install finds a kept folder and reuses the history.
//
// Safety, checked before anything is deleted (check()):
//   - the folder is the default work folder of `appIdentifier`
//     (WorkFolder::defaultPath, so it ends in /<appIdentifier> directly under
//     the per-user data folder), and never the home folder or a root;
//   - it is not a link, and its marker holds a well-formed id;
//   - no instance of the app holds its lock;
// Links (symbolic links, junctions, aliases) are removed as links; their
// targets are never touched.
class Uninstall {
public:
    struct Report {
        QStringList removed; // paths, as shown to the user
        QStringList kept;
        QStringList failed;  // "<path>: why"
        bool ok() const { return failed.isEmpty(); }
    };

    // The work folder entries "keep" leaves: <appIdentifier>.sqlite,
    // <appIdentifier>.sqlite-journal, backups, .workfolder,
    // known-models.json.
    static QStringList keptNames(const QString &appIdentifier);

    // The known paths outside the work folder that the app itself created
    // (PLAN 9.1a), on this system: macOS ~/Library/Caches/<appIdentifier>
    // (the browsers' cache twin folders and nothing else of ours); none on
    // Windows (the installer's own records are removed by its uninstaller)
    // and Linux.
    static QStringList knownPaths(const QString &appIdentifier);

    // Empty when `folder` may be uninstalled (see above), else why not.
    static QString check(const WorkFolder &folder);

    // Deletes the work folder (all of it, or all but keptNames()) and, for
    // the app's default work folder only, every known path. Takes the app's
    // lock first and holds it until everything but the marker is gone; when
    // the app holds it (it runs), nothing is removed. Call after check()
    // passed and after this process closed its database, its log and its own
    // lock. Every failure is reported; the rest is still tried.
    static Report run(const WorkFolder &folder, bool keepHistory);

    // The program itself, after an in-app uninstall (macOS and Linux; on
    // Windows the installer's uninstaller removes it):
    //   macOS  the app bundle the app runs from (<bundle>.app/Contents/MacOS)
    //          goes to the Trash (QFile::moveToTrash). When that fails - for
    //          example when macOS runs the app from a temporary read-only
    //          copy (App Translocation, which has no supported way to be
    //          detected) - the user is told to drag the app to the Trash.
    //   Linux  when the app runs from the package's folder
    //          (`linuxPackageBinDir`) and /usr/bin/pkexec exists, the
    //          package is removed with dpkg through pkexec (it asks for the
    //          administrator password); otherwise the user is told the
    //          command.
    // What happened, in plain words; *removed is true when the program is
    // gone.
    static QString removeProgram(const QString &packageName, const QString &linuxPackageBinDir, bool *removed);
};

} // namespace app
