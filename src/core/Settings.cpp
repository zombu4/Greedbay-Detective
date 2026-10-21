#include "core/Settings.h"

#include <cmath>

namespace app {

namespace {
const QString kSoldHistoryDaysKey = QStringLiteral("lookup/soldHistoryDays");
const QString kPreferredBrowserKey = QStringLiteral("browser/preferred");
const QString kUpdateCheckKey = QStringLiteral("updates/check");
const QString kModelEnabledKey = QStringLiteral("model/enabled");
const QString kModelIdKey = QStringLiteral("model/id");
const QString kModelGpuKey = QStringLiteral("model/gpu");
const QString kModelOfferKey = QStringLiteral("model/offerAnswered");
const QString kSharedReadKey = QStringLiteral("sharedlist/read");
const QString kSharedReportKey = QStringLiteral("sharedlist/report");
const QString kPageDelayMinKey = QStringLiteral("pace/pageDelayMinMs");
const QString kPageDelayMaxKey = QStringLiteral("pace/pageDelayMaxMs");
const QString kDailyPageCapKey = QStringLiteral("pace/dailyPageCap");
const QString kSoldDepthKey = QStringLiteral("lookup/soldDepth");
const QString kActiveDepthKey = QStringLiteral("lookup/activeDepth");
const QString kResearchDepthKey = QStringLiteral("lookup/researchDepth");
const QString kOutlierFactorKey = QStringLiteral("value/outlierIqrFactor");
const QString kConfidenceHighKey = QStringLiteral("value/confidenceHigh");
const QString kConfidenceMediumKey = QStringLiteral("value/confidenceMedium");
const QString kConfidenceLowKey = QStringLiteral("value/confidenceLow");
const QString kShippingRatioKey = QStringLiteral("shipping/flagRatio");
const QString kShippingMinKey = QStringLiteral("shipping/flagMinAboveCents");
const QString kEnabledPluginsKey = QStringLiteral("plugins/enabled");
const QString kPluginFailurePrefix = QStringLiteral("plugins/failed/");
const QString kPluginTimeLimitKey = QStringLiteral("plugins/callTimeLimitMs");
const QString kSellerThresholdKey = QStringLiteral("sellers/ratingThreshold");
const QString kSellerFewRatingsKey = QStringLiteral("sellers/fewRatings");
const QString kSellerCacheDaysKey = QStringLiteral("sellers/feedbackCacheDays");
constexpr int kMinPageDelayMs = 1000;

// The stored value as a finite number > 0, or `fallback`.
double storedPositive(const QSettings &settings, const QString &key, double fallback)
{
    bool ok = false;
    const double value = settings.value(key).toString().toDouble(&ok);
    return ok && std::isfinite(value) && value > 0 ? value : fallback;
}

bool isBrowserId(const QString &id)
{
    if (id.isEmpty() || id.size() > 32)
        return false;
    for (const QChar c : id) {
        if (c < QLatin1Char('a') || c > QLatin1Char('z'))
            return false;
    }
    return true;
}

// The stored value as an int, or -1 when absent or not an integer.
int storedInt(const QSettings &settings, const QString &key)
{
    bool ok = false;
    const int value = settings.value(key).toString().toInt(&ok);
    return ok ? value : -1;
}
} // namespace

Settings::Settings(const QString &iniPath)
    : m_settings(iniPath, QSettings::IniFormat)
{
}

QString Settings::fileName() const
{
    return m_settings.fileName();
}

int Settings::soldHistoryDays() const
{
    const QVariant value = m_settings.value(kSoldHistoryDaysKey);
    if (!value.isValid())
        return kDefaultSoldHistoryDays;
    bool ok = false;
    const int days = value.toString().toInt(&ok);
    return (ok && days >= 1 && days <= kMaxSoldHistoryDays) ? days : kDefaultSoldHistoryDays;
}

bool Settings::setSoldHistoryDays(int days)
{
    if (days < 1 || days > kMaxSoldHistoryDays)
        return false;
    m_settings.setValue(kSoldHistoryDaysKey, days);
    return true;
}

QString Settings::preferredBrowser() const
{
    const QString id = m_settings.value(kPreferredBrowserKey).toString();
    return isBrowserId(id) ? id : QString();
}

bool Settings::setPreferredBrowser(const QString &browserId)
{
    if (!browserId.isEmpty() && !isBrowserId(browserId))
        return false;
    m_settings.setValue(kPreferredBrowserKey, browserId);
    return true;
}

int Settings::pageDelayMinMs() const
{
    const int minMs = storedInt(m_settings, kPageDelayMinKey);
    const int maxMs = storedInt(m_settings, kPageDelayMaxKey);
    return (minMs >= kMinPageDelayMs && maxMs >= minMs) ? minMs : kDefaultPageDelayMinMs;
}

int Settings::pageDelayMaxMs() const
{
    const int minMs = storedInt(m_settings, kPageDelayMinKey);
    const int maxMs = storedInt(m_settings, kPageDelayMaxKey);
    return (minMs >= kMinPageDelayMs && maxMs >= minMs) ? maxMs : kDefaultPageDelayMaxMs;
}

bool Settings::setPageDelay(int minMs, int maxMs)
{
    if (minMs < kMinPageDelayMs || maxMs < minMs)
        return false;
    m_settings.setValue(kPageDelayMinKey, minMs);
    m_settings.setValue(kPageDelayMaxKey, maxMs);
    return true;
}

bool Settings::isPluginId(const QString &id)
{
    if (id.isEmpty() || id.size() > 64 || id.front() == QLatin1Char('-'))
        return false;
    for (const QChar c : id) {
        const bool allowed = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                             || c == QLatin1Char('-');
        if (!allowed)
            return false;
    }
    return true;
}

QStringList Settings::enabledPlugins() const
{
    QStringList ids;
    for (const QString &id : m_settings.value(kEnabledPluginsKey).toStringList()) {
        if (isPluginId(id) && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

bool Settings::setPluginEnabled(const QString &id, bool enabled)
{
    if (!isPluginId(id))
        return false;
    QStringList ids = enabledPlugins();
    ids.removeAll(id);
    if (enabled)
        ids.append(id);
    m_settings.setValue(kEnabledPluginsKey, ids);
    return true;
}

QString Settings::pluginFailure(const QString &id) const
{
    return isPluginId(id) ? m_settings.value(kPluginFailurePrefix + id).toString() : QString();
}

bool Settings::setPluginFailure(const QString &id, const QString &reason)
{
    if (!isPluginId(id))
        return false;
    if (reason.isEmpty())
        m_settings.remove(kPluginFailurePrefix + id);
    else
        m_settings.setValue(kPluginFailurePrefix + id, reason);
    return true;
}

int Settings::pluginCallTimeLimitMs() const
{
    const int ms = storedInt(m_settings, kPluginTimeLimitKey);
    return ms >= kMinPluginCallTimeLimitMs && ms <= kMaxPluginCallTimeLimitMs ? ms : kDefaultPluginCallTimeLimitMs;
}

bool Settings::setPluginCallTimeLimitMs(int ms)
{
    if (ms < kMinPluginCallTimeLimitMs || ms > kMaxPluginCallTimeLimitMs)
        return false;
    m_settings.setValue(kPluginTimeLimitKey, ms);
    return true;
}

int Settings::dailyPageCap() const
{
    const int cap = storedInt(m_settings, kDailyPageCapKey);
    return cap >= 1 ? cap : kDefaultDailyPageCap;
}

double Settings::sellerRatingThreshold() const
{
    const double value = storedPositive(m_settings, kSellerThresholdKey, kDefaultSellerRatingThreshold);
    return value <= 100 ? value : kDefaultSellerRatingThreshold;
}

int Settings::sellerFewRatings() const
{
    const int value = storedInt(m_settings, kSellerFewRatingsKey);
    return value >= 0 ? value : kDefaultSellerFewRatings;
}

int Settings::sellerFeedbackCacheDays() const
{
    const int value = storedInt(m_settings, kSellerCacheDaysKey);
    return value >= 0 && value <= kMaxSellerFeedbackCacheDays ? value : kDefaultSellerFeedbackCacheDays;
}

bool Settings::setSellerRatingThreshold(double percent)
{
    if (!std::isfinite(percent) || percent <= 0 || percent > 100)
        return false;
    m_settings.setValue(kSellerThresholdKey, QString::number(percent));
    return true;
}

bool Settings::setSellerFewRatings(int count)
{
    if (count < 0)
        return false;
    m_settings.setValue(kSellerFewRatingsKey, count);
    return true;
}

bool Settings::setSellerFeedbackCacheDays(int days)
{
    if (days < 0 || days > kMaxSellerFeedbackCacheDays)
        return false;
    m_settings.setValue(kSellerCacheDaysKey, days);
    return true;
}

bool Settings::setDailyPageCap(int cap)
{
    if (cap < 1)
        return false;
    m_settings.setValue(kDailyPageCapKey, cap);
    return true;
}

int Settings::soldDepth() const
{
    const int depth = storedInt(m_settings, kSoldDepthKey);
    return depth >= 0 ? depth : kDefaultSoldDepth;
}

int Settings::activeDepth() const
{
    const int depth = storedInt(m_settings, kActiveDepthKey);
    return depth >= 0 ? depth : kDefaultActiveDepth;
}

int Settings::researchDepth() const
{
    const int depth = storedInt(m_settings, kResearchDepthKey);
    return depth >= 0 ? depth : kDefaultResearchDepth;
}

bool Settings::setSoldDepth(int depth)
{
    if (depth < 0)
        return false;
    m_settings.setValue(kSoldDepthKey, depth);
    return true;
}

bool Settings::setActiveDepth(int depth)
{
    if (depth < 0)
        return false;
    m_settings.setValue(kActiveDepthKey, depth);
    return true;
}

bool Settings::setResearchDepth(int depth)
{
    if (depth < 0)
        return false;
    m_settings.setValue(kResearchDepthKey, depth);
    return true;
}

ValueSettings Settings::valueSettings() const
{
    const ValueSettings defaults;
    ValueSettings values;
    values.outlierIqrFactor = storedPositive(m_settings, kOutlierFactorKey, defaults.outlierIqrFactor);
    const int high = storedInt(m_settings, kConfidenceHighKey);
    const int medium = storedInt(m_settings, kConfidenceMediumKey);
    const int low = storedInt(m_settings, kConfidenceLowKey);
    const int h = high >= 1 ? high : defaults.confidenceHigh;
    const int m = medium >= 1 ? medium : defaults.confidenceMedium;
    const int l = low >= 1 ? low : defaults.confidenceLow;
    if (h >= m && m >= l) {
        values.confidenceHigh = h;
        values.confidenceMedium = m;
        values.confidenceLow = l;
    }
    values.shippingFlagRatio = storedPositive(m_settings, kShippingRatioKey, defaults.shippingFlagRatio);
    bool ok = false;
    const qint64 minimum = m_settings.value(kShippingMinKey).toString().toLongLong(&ok);
    values.shippingFlagMinAboveCents = ok && minimum >= 0 ? minimum : defaults.shippingFlagMinAboveCents;
    return values;
}

bool Settings::setValueSettings(const ValueSettings &values)
{
    if (!(std::isfinite(values.outlierIqrFactor) && values.outlierIqrFactor > 0) || values.confidenceLow < 1
        || values.confidenceMedium < values.confidenceLow || values.confidenceHigh < values.confidenceMedium
        || !(std::isfinite(values.shippingFlagRatio) && values.shippingFlagRatio > 0) || values.shippingFlagMinAboveCents < 0)
        return false;
    m_settings.setValue(kOutlierFactorKey, values.outlierIqrFactor);
    m_settings.setValue(kConfidenceHighKey, values.confidenceHigh);
    m_settings.setValue(kConfidenceMediumKey, values.confidenceMedium);
    m_settings.setValue(kConfidenceLowKey, values.confidenceLow);
    m_settings.setValue(kShippingRatioKey, values.shippingFlagRatio);
    m_settings.setValue(kShippingMinKey, values.shippingFlagMinAboveCents);
    return true;
}

bool Settings::updateCheck() const
{
    return m_settings.value(kUpdateCheckKey).toString() == QLatin1String("true");
}

void Settings::setUpdateCheck(bool enabled)
{
    m_settings.setValue(kUpdateCheckKey, enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

bool Settings::modelEnabled() const { return m_settings.value(kModelEnabledKey).toString() == QLatin1String("true"); }

void Settings::setModelEnabled(bool enabled)
{
    m_settings.setValue(kModelEnabledKey, enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

QString Settings::modelId() const { return m_settings.value(kModelIdKey).toString(); }

void Settings::setModelId(const QString &id) { m_settings.setValue(kModelIdKey, id); }

bool Settings::modelGpu() const { return m_settings.value(kModelGpuKey).toString() != QLatin1String("false"); }

void Settings::setModelGpu(bool enabled)
{
    m_settings.setValue(kModelGpuKey, enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

bool Settings::modelOfferAnswered() const { return m_settings.value(kModelOfferKey).toString() == QLatin1String("true"); }

void Settings::setModelOfferAnswered() { m_settings.setValue(kModelOfferKey, QStringLiteral("true")); }

bool Settings::sharedListRead() const
{
    return m_settings.value(kSharedReadKey).toString() == QLatin1String("true");
}

void Settings::setSharedListRead(bool enabled)
{
    m_settings.setValue(kSharedReadKey, enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

bool Settings::sharedListReport() const
{
    return m_settings.value(kSharedReportKey).toString() == QLatin1String("true");
}

void Settings::setSharedListReport(bool enabled)
{
    m_settings.setValue(kSharedReportKey, enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

bool Settings::sync()
{
    m_settings.sync();
    return m_settings.status() == QSettings::NoError;
}

} // namespace app
