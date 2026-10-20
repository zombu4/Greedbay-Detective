#pragma once

#include "core/WorkFolder.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace app {
class Settings;
}

namespace app::ui {

// The Settings screen (inbox phase 6 step 7, PLAN 14): every value is read
// from and written to the work folder's settings.ini (core/Settings) at once.
// Each set...() returns an empty string when the value was stored, else the
// reason in plain words (nothing is stored then).
class SettingsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    // [{id, name}]: "" = automatic (the first installed browser in the
    // preference order), then every installed supported browser.
    Q_PROPERTY(QVariantList browsers READ browsers NOTIFY changed)
    Q_PROPERTY(QString preferredBrowser READ preferredBrowser NOTIFY changed)
    // The browser a lookup would use now, or a sentence that none is installed.
    Q_PROPERTY(QString browserInUse READ browserInUse NOTIFY changed)
    Q_PROPERTY(QString workFolder READ workFolder CONSTANT)
    Q_PROPERTY(int pageDelayMinSeconds READ pageDelayMinSeconds NOTIFY changed)
    Q_PROPERTY(int pageDelayMaxSeconds READ pageDelayMaxSeconds NOTIFY changed)
    Q_PROPERTY(int dailyPageCap READ dailyPageCap NOTIFY changed)
    // Sellers (owner decisions 2026-10-20).
    Q_PROPERTY(double sellerRatingThreshold READ sellerRatingThreshold NOTIFY changed)
    Q_PROPERTY(int sellerFewRatings READ sellerFewRatings NOTIFY changed)
    Q_PROPERTY(int sellerFeedbackCacheDays READ sellerFeedbackCacheDays NOTIFY changed)
    // "standard" when the three depths are the Standard preset, else "custom".
    Q_PROPERTY(QString depthPreset READ depthPreset NOTIFY changed)
    Q_PROPERTY(QString standardPresetText READ standardPresetText CONSTANT)
    Q_PROPERTY(int soldDepth READ soldDepth NOTIFY changed)
    Q_PROPERTY(int activeDepth READ activeDepth NOTIFY changed)
    Q_PROPERTY(int researchDepth READ researchDepth NOTIFY changed)
    Q_PROPERTY(int historyDays READ historyDays NOTIFY changed)
    Q_PROPERTY(int maxHistoryDays READ maxHistoryDays CONSTANT)
    Q_PROPERTY(bool updateCheck READ updateCheck NOTIFY changed)
    Q_PROPERTY(double outlierFactor READ outlierFactor NOTIFY changed)
    Q_PROPERTY(int confidenceHigh READ confidenceHigh NOTIFY changed)
    Q_PROPERTY(int confidenceMedium READ confidenceMedium NOTIFY changed)
    Q_PROPERTY(int confidenceLow READ confidenceLow NOTIFY changed)
    Q_PROPERTY(double shippingRatio READ shippingRatio NOTIFY changed)
    Q_PROPERTY(double shippingMinAboveDollars READ shippingMinAboveDollars NOTIFY changed)
    // The IBM Plex font license (SIL Open Font License 1.1), shipped with the app.
    Q_PROPERTY(QString fontLicense READ fontLicense CONSTANT)
public:
    // `sessionRunning` tells whether a lookup's browser session runs (no
    // sign-out then).
    SettingsController(WorkFolder folder, std::function<bool()> sessionRunning, QObject *parent = nullptr);

    QVariantList browsers() const;
    QString preferredBrowser() const;
    QString browserInUse() const;
    QString workFolder() const;
    int pageDelayMinSeconds() const;
    int pageDelayMaxSeconds() const;
    int dailyPageCap() const;
    double sellerRatingThreshold() const;
    int sellerFewRatings() const;
    int sellerFeedbackCacheDays() const;
    QString depthPreset() const;
    QString standardPresetText() const;
    int soldDepth() const;
    int activeDepth() const;
    int researchDepth() const;
    int historyDays() const;
    int maxHistoryDays() const;
    bool updateCheck() const;
    double outlierFactor() const;
    int confidenceHigh() const;
    int confidenceMedium() const;
    int confidenceLow() const;
    double shippingRatio() const;
    double shippingMinAboveDollars() const;
    QString fontLicense() const;

    Q_INVOKABLE QString setPreferredBrowser(const QString &browserId);
    Q_INVOKABLE QString setPageDelaySeconds(int minSeconds, int maxSeconds);
    Q_INVOKABLE QString setDailyPageCap(int cap);
    // An empty answer: stored (and sellerRulesChanged); else why not.
    Q_INVOKABLE QString setSellerRules(double threshold, int fewRatings, int cacheDays);
    Q_INVOKABLE QString useStandardDepths();
    Q_INVOKABLE QString setDepths(int sold, int active, int research);
    Q_INVOKABLE QString setHistoryDays(int days);
    Q_INVOKABLE QString setNumbers(double outlierFactor, int high, int medium, int low, double shippingRatio,
                                   double shippingMinAboveDollars);
    // Deletes the app's profile folder of every supported browser (core/
    // BrowserProfiles::clear; a browser chosen earlier may have one too): the
    // eBay sign-in, cookies and cache of the app's browsers. Returns
    // {ok: bool, message: what happened, in plain words}.
    Q_INVOKABLE QVariantMap signOut();

    // Reads the installed browsers again.
    Q_INVOKABLE void refreshBrowsers();

signals:
    void sellerRulesChanged();
    void changed();

private:
    // Syncs `settings` (which holds the change) when `ok`.
    QString stored(Settings &settings, bool ok, const QString &refusal);

    WorkFolder m_folder;
    std::function<bool()> m_sessionRunning;
    QVariantList m_browsers;
    QString m_browserInUse;
};

} // namespace app::ui
