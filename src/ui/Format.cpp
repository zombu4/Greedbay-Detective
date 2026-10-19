#include "ui/Format.h"

#include "lookup/LookupReport.h"

#include <QLocale>

namespace app::ui {

QString moneyText(const std::optional<qint64> &cents)
{
    return cents ? lookup::money(*cents) : QStringLiteral("-");
}

QString whenText(const QDateTime &time, const QDateTime &now)
{
    if (!time.isValid())
        return QStringLiteral("-");
    const QDateTime local = time.toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    if (local.date() == today)
        return QStringLiteral("today ") + english.toString(local.time(), QStringLiteral("HH:mm"));
    if (local.date() == today.addDays(-1))
        return QStringLiteral("yesterday ") + english.toString(local.time(), QStringLiteral("HH:mm"));
    return english.toString(local.date(), QStringLiteral("MMM d, yyyy"));
}

QString countText(qsizetype count, const QString &singular, const QString &plural)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : plural);
}

} // namespace app::ui
