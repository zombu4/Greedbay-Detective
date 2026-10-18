#pragma once

#include <QSettings>
#include <QString>

namespace app {

// User settings, stored only in an INI file at an explicit path.
//
// QSettings' native format would write to the macOS plist folder, the Windows
// registry or ~/.config, outside the work folder. The work folder must hold
// everything the app stores, so the INI format and an explicit file path are
// always used and the platform default store is never touched.
class Settings {
public:
    static constexpr int kDefaultSoldHistoryDays = 90;

    explicit Settings(const QString &iniPath);

    QString fileName() const;

    // Key "lookup/soldHistoryDays". Returns kDefaultSoldHistoryDays when the
    // key is absent, is not an integer, or is below 1.
    int soldHistoryDays() const;

    // Returns false and stores nothing when days < 1. No upper bound is
    // enforced yet: the limit will be the longest period eBay's Product
    // Research offers, which is still to be confirmed.
    bool setSoldHistoryDays(int days);

    // Writes pending changes to disk. True when QSettings reports no error.
    bool sync();

private:
    QSettings m_settings;
};

} // namespace app
