#pragma once

#include "core/WorkFolder.h"

#include <QString>

namespace app {

// The profiles of the installed browsers the app drives. Each browser gets
// its own profile folder in the work folder, <work>/browser/<id>/ (passed to
// the browser as its user data folder), and a lock file <work>/browser/<id>.lock
// that the app holds for as long as its instance of that browser runs.
// Code that changes a profile takes the same lock first, so it never runs
// while the browser uses the files.
class BrowserProfiles {
public:
    // Browser ids are short lowercase names ("brave", "chrome", "edge",
    // "chromium"); anything else is refused, so an id can never form a path
    // outside browser/.
    static bool isValidId(const QString &browserId);

    static QString profileDir(const WorkFolder &folder, const QString &browserId);
    static QString lockPath(const WorkFolder &folder, const QString &browserId);

    // Sign out: deletes that browser's whole profile folder (cookies,
    // storage, cache, everything the browser keeps there). Never starts the
    // browser. Links inside are removed as links; their targets are never
    // touched.
    //
    // Refuses (returns false, sets *error, deletes nothing) when the id is not
    // valid, when the app's instance of that browser runs (the lock is held),
    // or when the profile folder is itself a link or not a folder. Returns
    // true when there is no profile folder. If a deletion fails part way, the
    // remaining entries are still tried and false is returned with the first
    // error.
    static bool clear(const WorkFolder &folder, const QString &browserId, QString *error);
};

} // namespace app
