#pragma once

#include "core/WorkFolder.h"

#include <QString>

namespace app {

// The browser's data in the work folder (webengine/): cookies and storage,
// HTTP cache and the download folder, plus the session lock.
//
// The browser worker holds the session lock (webengine/session.lock) from
// before it creates its browser profile until its process has shut Qt
// WebEngine down. Code that changes the browser data takes the same lock
// first, so it never runs while a browser session may use the files.
class WebEngineData {
public:
    static QString sessionLockPath(const WorkFolder &folder);

    // Sign out and clear the cache: deletes everything inside webengine/
    // (cookies, storage, cache, downloads, anything else) except the session
    // lock file, then creates webengine/storage, webengine/cache and
    // webengine/downloads again, empty. Never starts the browser.
    //
    // Links inside are removed as links; their targets are never touched.
    // Refuses (returns false, sets *error, deletes nothing) when a browser
    // session holds the session lock, or when webengine/ itself is a link or
    // not a folder. If a deletion fails part way, the remaining entries are
    // still tried and false is returned with the first error.
    static bool clear(const WorkFolder &folder, QString *error);
};

} // namespace app
