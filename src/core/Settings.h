#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace app {

// The numbers of the value math (PLAN 6 and 14, DESIGN 3.4).
struct ValueSettings {
    // Totals outside [P25 - f x IQR, P75 + f x IQR] are outliers.
    double outlierIqrFactor = 1.5;
    // Confidence by the number of included sales: High >= high, Medium >=
    // medium, Low >= low, else not enough data.
    int confidenceHigh = 10;
    int confidenceMedium = 5;
    int confidenceLow = 2;
    // The listing's shipping is flagged when it is more than ratio x the
    // median shipping AND more than minAboveCents above it.
    double shippingFlagRatio = 2.0;
    qint64 shippingFlagMinAboveCents = 1000;
};

// User settings, stored only in an INI file at an explicit path.
//
// QSettings' native format would write to the macOS plist folder, the Windows
// registry or ~/.config, outside the work folder. The work folder must hold
// everything the app stores, so the INI format and an explicit file path are
// always used and the platform default store is never touched.
class Settings {
public:
    // Owner decision 2026-10-19 (replaces Q20's 90 days): Product Research
    // uses 3 years by default. Its first page is one load either way; more
    // rows can mean more pages, up to the research depth.
    static constexpr int kDefaultSoldHistoryDays = 1095;
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

    // The longest Product Research period: dayRange=1095 shows "Last 3
    // years" (recon section 4; owner decision Q20: up to the longest period
    // Product Research offers).
    static constexpr int kMaxSoldHistoryDays = 1095;

    // Key "lookup/soldHistoryDays". Returns kDefaultSoldHistoryDays when the
    // key is absent, is not an integer, or is outside 1..kMaxSoldHistoryDays.
    int soldHistoryDays() const;

    // Returns false and stores nothing when days is outside
    // 1..kMaxSoldHistoryDays.
    bool setSoldHistoryDays(int days);

    // Key "updates/check": whether the app may check for a new version
    // (owner answer 6: off by default, opt-in). Anything but "true" reads as
    // off.
    bool updateCheck() const;
    void setUpdateCheck(bool enabled);

    // The local model (DESIGN 3.6; owner: default Off). "model/enabled":
    // anything but "true" is off. "model/id": a model of the catalog (empty:
    // none chosen; the app's default is used). "model/gpu": the graphics
    // chip is used unless "false". "model/offerAnswered": the first-run offer
    // on Home was answered ("true") and is not shown again.
    bool modelEnabled() const;
    void setModelEnabled(bool enabled);
    QString modelId() const;
    void setModelId(const QString &id);
    bool modelGpu() const;
    void setModelGpu(bool enabled);
    bool modelOfferAnswered() const;
    void setModelOfferAnswered();

    // Keys "sharedlist/read" and "sharedlist/report": whether the app reads
    // the shared seller list and may send reports to it (owner decision
    // 2026-10-20: opt-in, both off by default). Anything but "true" is off.
    bool sharedListRead() const;
    void setSharedListRead(bool enabled);
    bool sharedListReport() const;
    void setSharedListReport(bool enabled);

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

    // Keys "value/outlierIqrFactor", "value/confidenceHigh",
    // "value/confidenceMedium", "value/confidenceLow", "shipping/flagRatio",
    // "shipping/flagMinAboveCents". A value that is missing or out of range
    // reads as its default; the three confidence numbers must satisfy
    // high >= medium >= low >= 1, else all three read as their defaults.
    ValueSettings valueSettings() const;
    // Returns false and stores nothing when a value is out of range (factor
    // and ratio must be > 0 and finite, the minimum >= 0).
    bool setValueSettings(const ValueSettings &values);

    // Plugins (DESIGN 5, PLAN 15). Key "plugins/enabled": the ids of the
    // enabled plugins. None by default: a plugin stays disabled until the
    // user enables it. Values that are no plugin id (isPluginId) are skipped.
    QStringList enabledPlugins() const;
    // Returns false and stores nothing for an id that is no plugin id.
    bool setPluginEnabled(const QString &id, bool enabled);
    // Key "plugins/failed/<id>": why the app disabled the plugin after a
    // failure; empty when it did not. An empty reason removes the key.
    QString pluginFailure(const QString &id) const;
    bool setPluginFailure(const QString &id, const QString &reason);
    // Key "plugins/callTimeLimitMs": the longest one plugin call may run
    // before it is stopped. Default 1000; a value outside
    // kMinPluginCallTimeLimitMs..kMaxPluginCallTimeLimitMs reads as the
    // default.
    static constexpr int kDefaultPluginCallTimeLimitMs = 1000;
    static constexpr int kMinPluginCallTimeLimitMs = 100;
    static constexpr int kMaxPluginCallTimeLimitMs = 60000;
    int pluginCallTimeLimitMs() const;
    // Returns false and stores nothing outside the range above.
    bool setPluginCallTimeLimitMs(int ms);

    // Sellers (owner decisions 2026-10-20). Key "sellers/ratingThreshold":
    // a seller whose positive percentage is below it is flagged (default
    // 96.0; outside 0 < value <= 100 reads as the default). Key
    // "sellers/fewRatings": with fewer ratings the app says "only N ratings"
    // instead of judging (default 20; below 0 reads as the default). Key
    // "sellers/feedbackCacheDays": a seller's reviews read from eBay are
    // shown again without a page load for this many days (default 7; 0 =
    // always read again; below 0 or above 365 reads as the default).
    static constexpr double kDefaultSellerRatingThreshold = 96.0;
    static constexpr int kDefaultSellerFewRatings = 20;
    static constexpr int kDefaultSellerFeedbackCacheDays = 7;
    static constexpr int kMaxSellerFeedbackCacheDays = 365;
    double sellerRatingThreshold() const;
    int sellerFewRatings() const;
    int sellerFeedbackCacheDays() const;
    // Each returns false and stores nothing outside its range.
    bool setSellerRatingThreshold(double percent);
    bool setSellerFewRatings(int count);
    bool setSellerFeedbackCacheDays(int days);

    // A plugin id: 1 to 64 characters, lowercase a-z, 0-9 and "-", starting
    // with a letter or digit.
    static bool isPluginId(const QString &id);

    // Writes pending changes to disk. True when QSettings reports no error.
    bool sync();

private:
    QSettings m_settings;
};

} // namespace app
