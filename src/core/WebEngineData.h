#pragma once

#include "core/WorkFolder.h"

#include <QString>

namespace app {

// The browser's data in the work folder (webengine/): cookies and storage,
// HTTP cache and the download folder, plus the session lock.
//
// The browser worker holds the session lock (webengine/session.lock) from
// before it creates its browser profile until after the profile is deleted.
// Code that changes the browser data takes the same lock first, so it never
// runs while a browser session uses the files.
class WebEngineData {
public:
    static QString sessionLockPath(const WorkFolder &folder);

    // Sign out and clear the cache: deletes everything inside
    // webengine/storage, webengine/cache and webengine/downloads. The three
    // folders themselves stay. Never starts the browser.
    //
    // Links inside are removed as links; their targets are never touched.
    // Refuses (returns false, sets *error, deletes nothing) when a browser
    // session holds the session lock, or when one of the three folders is
    // itself a link or not a folder. If a deletion fails part way, the
    // remaining entries are still tried and false is returned with the first
    // error.
    static bool clear(const WorkFolder &folder, QString *error);
};

} // namespace app
