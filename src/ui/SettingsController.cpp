#include "ui/SettingsController.h"

#include "browser/BrowserCatalog.h"
#include "core/BrowserProfiles.h"
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QVariantMap>

#include <cmath>
#include <utility>

namespace app::ui {

namespace {

constexpr int kMsPerSecond = 1000;

bool isStandard(const Settings &settings)
{
    return settings.soldDepth() == Settings::kDefaultSoldDepth && settings.activeDepth() == Settings::kDefaultActiveDepth
           && settings.researchDepth() == Settings::kDefaultResearchDepth;
}

} // namespace

SettingsController::SettingsController(WorkFolder folder, std::function<bool()> sessionRunning, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_sessionRunning(std::move(sessionRunning))
{
    refreshBrowsers();
}

void SettingsController::refreshBrowsers()
{
    const QList<browser::InstalledBrowser> installed = browser::BrowserCatalog::installed();
    m_browsers.clear();
    m_browsers.append(QVariantMap{{QStringLiteral("id"), QString()},
                                  {QStringLiteral("name"), QStringLiteral("Automatic (first installed)")}});
    for (const browser::InstalledBrowser &browser : installed)
        m_browsers.append(QVariantMap{{QStringLiteral("id"), browser.id}, {QStringLiteral("name"), browser.name}});
    const std::optional<browser::InstalledBrowser> chosen =
        browser::BrowserCatalog::choose(installed, Settings(m_folder.settingsPath()).preferredBrowser());
    m_browserInUse = chosen ? QStringLiteral("%1 (%2)").arg(chosen->name, QDir::toNativeSeparators(chosen->executable))
                            : QStringLiteral("No supported browser is installed: Brave, Google Chrome or Microsoft Edge "
                                             "(on Linux also Chromium).");
    emit changed();
}

QString SettingsController::stored(Settings &settings, bool ok, const QString &refusal)
{
    if (!ok)
        return refusal;
    if (!settings.sync())
        return QStringLiteral("The settings file cannot be written.");
    emit changed();
    return {};
}

QVariantList SettingsController::browsers() const { return m_browsers; }
QString SettingsController::preferredBrowser() const { return Settings(m_folder.settingsPath()).preferredBrowser(); }
QString SettingsController::browserInUse() const { return m_browserInUse; }
QString SettingsController::workFolder() const { return QDir::toNativeSeparators(m_folder.path()); }
int SettingsController::pageDelayMinSeconds() const { return Settings(m_folder.settingsPath()).pageDelayMinMs() / kMsPerSecond; }
int SettingsController::pageDelayMaxSeconds() const { return Settings(m_folder.settingsPath()).pageDelayMaxMs() / kMsPerSecond; }
int SettingsController::dailyPageCap() const { return Settings(m_folder.settingsPath()).dailyPageCap(); }
double SettingsController::sellerRatingThreshold() const { return Settings(m_folder.settingsPath()).sellerRatingThreshold(); }
int SettingsController::sellerFewRatings() const { return Settings(m_folder.settingsPath()).sellerFewRatings(); }
int SettingsController::sellerFeedbackCacheDays() const { return Settings(m_folder.settingsPath()).sellerFeedbackCacheDays(); }

QString SettingsController::depthPreset() const
{
    return isStandard(Settings(m_folder.settingsPath())) ? QStringLiteral("standard") : QStringLiteral("custom");
}

QString SettingsController::standardPresetText() const
{
    return QStringLiteral("Standard: %1 sold, %2 for sale, up to %3 Product Research rows")
        .arg(Settings::kDefaultSoldDepth)
        .arg(Settings::kDefaultActiveDepth)
        .arg(Settings::kDefaultResearchDepth);
}

int SettingsController::soldDepth() const { return Settings(m_folder.settingsPath()).soldDepth(); }
int SettingsController::activeDepth() const { return Settings(m_folder.settingsPath()).activeDepth(); }
int SettingsController::researchDepth() const { return Settings(m_folder.settingsPath()).researchDepth(); }
int SettingsController::historyDays() const { return Settings(m_folder.settingsPath()).soldHistoryDays(); }
int SettingsController::maxHistoryDays() const { return Settings::kMaxSoldHistoryDays; }
bool SettingsController::updateCheck() const { return Settings(m_folder.settingsPath()).updateCheck(); }
double SettingsController::outlierFactor() const { return Settings(m_folder.settingsPath()).valueSettings().outlierIqrFactor; }
int SettingsController::confidenceHigh() const { return Settings(m_folder.settingsPath()).valueSettings().confidenceHigh; }
int SettingsController::confidenceMedium() const { return Settings(m_folder.settingsPath()).valueSettings().confidenceMedium; }
int SettingsController::confidenceLow() const { return Settings(m_folder.settingsPath()).valueSettings().confidenceLow; }
double SettingsController::shippingRatio() const { return Settings(m_folder.settingsPath()).valueSettings().shippingFlagRatio; }

double SettingsController::shippingMinAboveDollars() const
{
    return static_cast<double>(Settings(m_folder.settingsPath()).valueSettings().shippingFlagMinAboveCents) / 100.0;
}

QString SettingsController::fontLicense() const
{
    QFile file(QStringLiteral(":/licenses/IBM-Plex-LICENSE.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("The font license text is missing from this build.");
    return QString::fromUtf8(file.readAll());
}

QString SettingsController::setPreferredBrowser(const QString &browserId)
{
    Settings settings(m_folder.settingsPath());
    const QString result = stored(settings, settings.setPreferredBrowser(browserId), QStringLiteral("Not a browser the app knows."));
    refreshBrowsers();
    return result;
}

QString SettingsController::setPageDelaySeconds(int minSeconds, int maxSeconds)
{
    Settings settings(m_folder.settingsPath());
    return stored(settings, settings.setPageDelay(minSeconds * kMsPerSecond, maxSeconds * kMsPerSecond),
                  QStringLiteral("The wait must be at least 1 second, and the longest wait at least the shortest."));
}

QString SettingsController::setSellerRules(double threshold, int fewRatings, int cacheDays)
{
    Settings settings(m_folder.settingsPath());
    if (!(threshold > 0 && threshold <= 100))
        return QStringLiteral("The rating limit must be above 0 and at most 100%.");
    if (fewRatings < 0)
        return QStringLiteral("The number of ratings cannot be negative.");
    if (cacheDays < 0 || cacheDays > Settings::kMaxSellerFeedbackCacheDays)
        return QStringLiteral("Keep reviews for 0 to %1 days.").arg(Settings::kMaxSellerFeedbackCacheDays);
    return stored(settings,
                  settings.setSellerRatingThreshold(threshold) && settings.setSellerFewRatings(fewRatings)
                      && settings.setSellerFeedbackCacheDays(cacheDays),
                  QStringLiteral("The seller settings were not stored."));
}

QString SettingsController::setDailyPageCap(int cap)
{
    Settings settings(m_folder.settingsPath());
    return stored(settings, settings.setDailyPageCap(cap), QStringLiteral("The daily cap must be at least 1 page."));
}

QString SettingsController::useStandardDepths()
{
    return setDepths(Settings::kDefaultSoldDepth, Settings::kDefaultActiveDepth, Settings::kDefaultResearchDepth);
}

QString SettingsController::setDepths(int sold, int active, int research)
{
    if (sold < 0 || active < 0 || research < 0)
        return QStringLiteral("A depth cannot be negative (0 skips that source).");
    Settings settings(m_folder.settingsPath());
    const bool ok = settings.setSoldDepth(sold) && settings.setActiveDepth(active) && settings.setResearchDepth(research);
    return stored(settings, ok, QStringLiteral("A depth cannot be negative (0 skips that source)."));
}

QString SettingsController::setHistoryDays(int days)
{
    Settings settings(m_folder.settingsPath());
    return stored(settings, settings.setSoldHistoryDays(days),
                  QStringLiteral("The period must be 1 to %1 days (Product Research offers up to 3 years).")
                      .arg(Settings::kMaxSoldHistoryDays));
}

QString SettingsController::setNumbers(double outlierFactor, int high, int medium, int low, double shippingRatio,
                                       double shippingMinAboveDollars)
{
    if (!(high >= medium && medium >= low && low >= 1))
        return QStringLiteral("The confidence counts must be High >= Medium >= Low >= 1.");
    if (!std::isfinite(shippingMinAboveDollars) || shippingMinAboveDollars < 0)
        return QStringLiteral("The shipping amount cannot be negative.");
    ValueSettings values;
    values.outlierIqrFactor = outlierFactor;
    values.confidenceHigh = high;
    values.confidenceMedium = medium;
    values.confidenceLow = low;
    values.shippingFlagRatio = shippingRatio;
    values.shippingFlagMinAboveCents = std::llround(shippingMinAboveDollars * 100.0);
    Settings settings(m_folder.settingsPath());
    return stored(settings, settings.setValueSettings(values),
                  QStringLiteral("The outlier factor and the shipping ratio must be numbers above 0."));
}

QVariantMap SettingsController::signOut()
{
    auto reply = [](bool ok, const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), ok}, {QStringLiteral("message"), message}};
    };
    if (m_sessionRunning && m_sessionRunning())
        return reply(false, QStringLiteral("A lookup is running: sign out after it has finished."));
    QStringList failed;
    for (const QString &id : browser::BrowserCatalog::preferenceOrder()) {
        QString error;
        if (!BrowserProfiles::clear(m_folder, id, &error))
            failed.append(QStringLiteral("%1: %2").arg(id, error));
    }
    if (!failed.isEmpty())
        return reply(false, QStringLiteral("Sign out failed for %1").arg(failed.join(QStringLiteral("; "))));
    return reply(true, QStringLiteral("Signed out: the app's browser profiles (eBay sign-in, cookies, cache) were deleted "
                                      "from the work folder. Your own browser profiles were not touched."));
}

} // namespace app::ui
