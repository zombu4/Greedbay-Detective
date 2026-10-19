#include "core/Settings.h"

namespace app {

namespace {
const QString kSoldHistoryDaysKey = QStringLiteral("lookup/soldHistoryDays");
const QString kPreferredBrowserKey = QStringLiteral("browser/preferred");
const QString kPageDelayMinKey = QStringLiteral("pace/pageDelayMinMs");
const QString kPageDelayMaxKey = QStringLiteral("pace/pageDelayMaxMs");
const QString kDailyPageCapKey = QStringLiteral("pace/dailyPageCap");
const QString kSoldDepthKey = QStringLiteral("lookup/soldDepth");
const QString kActiveDepthKey = QStringLiteral("lookup/activeDepth");
const QString kResearchDepthKey = QStringLiteral("lookup/researchDepth");
constexpr int kMinPageDelayMs = 1000;

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
    return (ok && days >= 1) ? days : kDefaultSoldHistoryDays;
}

bool Settings::setSoldHistoryDays(int days)
{
    if (days < 1)
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

int Settings::dailyPageCap() const
{
    const int cap = storedInt(m_settings, kDailyPageCapKey);
    return cap >= 1 ? cap : kDefaultDailyPageCap;
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

bool Settings::sync()
{
    m_settings.sync();
    return m_settings.status() == QSettings::NoError;
}

} // namespace app
