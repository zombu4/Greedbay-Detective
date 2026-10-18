#include "core/Settings.h"

namespace app {

namespace {
const QString kSoldHistoryDaysKey = QStringLiteral("lookup/soldHistoryDays");
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

bool Settings::sync()
{
    m_settings.sync();
    return m_settings.status() == QSettings::NoError;
}

} // namespace app
