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
    // Owner decision 2026-10-18: 3-6 s randomized between page loads.
    static constexpr int kDefaultPageDelayMinMs = 3000;
    static constexpr int kDefaultPageDelayMaxMs = 6000;
    // Owner decision: at most 300 eBay page loads per day.
    static constexpr int kDefaultDailyPageCap = 300;
    // Owner answer 4 ("Standard"): 240 sold, 240 for sale now, up to 200
    // Product Research rows per lookup.
    static constexpr int kDefaultSoldDepth = 240;
    static constexpr int kDefaultActiveDepth = 240;
    static constexpr int kDefaultResearchDepth = 200;

    explicit Settings(const QString &iniPath);

    QString fileName() const;

    // Key "lookup/soldHistoryDays". Returns kDefaultSoldHistoryDays when the
    // key is absent, is not an integer, or is below 1.
    int soldHistoryDays() const;

    // Returns false and stores nothing when days < 1. No upper bound is
    // enforced yet: the limit will be the longest period eBay's Product
    // Research offers, which is still to be confirmed.
    bool setSoldHistoryDays(int days);

    // Key "browser/preferred": the id of the browser the user picked in
    // Settings ("brave", "chrome", "edge", "chromium"), or an empty string for
    // "automatic" (the first installed browser in the preference order).
    // Anything that is not a lowercase id reads as empty.
    QString preferredBrowser() const;
    // Returns false and stores nothing for an id that is not lowercase a-z.
    // An empty string stores "automatic".
    bool setPreferredBrowser(const QString &browserId);

    // Keys "pace/pageDelayMinMs" and "pace/pageDelayMaxMs": the app waits a
    // random time in [min, max] between page loads. Defaults 3000 and 6000.
    // Values below 1000 ms, or a max below the min, read as the defaults.
    int pageDelayMinMs() const;
    int pageDelayMaxMs() const;
    // Returns false and stores nothing when minMs < 1000 or maxMs < minMs.
    bool setPageDelay(int minMs, int maxMs);

    // Key "pace/dailyPageCap": the most eBay pages the app loads per day
    // (core/PageBudget counts them). Default 300; a value below 1 reads as
    // the default.
    int dailyPageCap() const;
    // Returns false and stores nothing when cap < 1.
    bool setDailyPageCap(int cap);

    // Keys "lookup/soldDepth", "lookup/activeDepth", "lookup/researchDepth":
    // how many records a lookup collects at most from the sold search, the
    // active search and Product Research. 0 skips that source. Defaults 240,
    // 240, 200; a negative or non-integer value reads as the default. No
    // upper bound: the daily page cap limits the page loads.
    int soldDepth() const;
    int activeDepth() const;
    int researchDepth() const;
    // Each returns false and stores nothing for a negative depth.
    bool setSoldDepth(int depth);
    bool setActiveDepth(int depth);
    bool setResearchDepth(int depth);

    // Writes pending changes to disk. True when QSettings reports no error.
    bool sync();

private:
    QSettings m_settings;
};

} // namespace app
