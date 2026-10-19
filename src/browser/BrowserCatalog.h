#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::browser {

// An installed browser the app can drive.
struct InstalledBrowser {
    QString id;         // "brave", "chrome", "edge", "chromium" (also the profile folder name)
    QString name;       // shown to the user: "Brave", "Google Chrome", ...
    QString executable; // full path of the program to start
};

// Finds the installed browsers the app can drive and picks one.
//
// Supported: Chromium-based browsers, driven over the DevTools protocol
// (see CdpConnection). Owner decision, preference order: Brave, Google Chrome,
// Microsoft Edge, and on Linux also Chromium. Firefox, also in the owner's
// list, is not driven: while its remote control is active it reports
// navigator.webdriver = true to every page (PLAN 7.3), which a site sees.
//
// Lookup per system, by fixed names only (nothing is searched):
//   macOS    Launch Services, by bundle identifier
//   Windows  the "App Paths" registry entry of the program (per user, then
//            per machine)
//   Linux    the program names on PATH
class BrowserCatalog {
public:
    // Every supported browser that is installed, in preference order.
    static QList<InstalledBrowser> installed();

    // The browser to use: the one with `preferredId` when it is installed,
    // otherwise the first one of `installed`. std::nullopt when the list is
    // empty.
    static std::optional<InstalledBrowser> choose(const QList<InstalledBrowser> &installed,
                                                  const QString &preferredId);

    // The preference order of browser ids on this system.
    static QStringList preferenceOrder();
};

} // namespace app::browser
