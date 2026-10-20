#include "lookup/AuctionTime.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

namespace app::lookup {

namespace {

constexpr qint64 kMinuteMs = 60 * 1000;

struct Relative {
    qint64 ms = 0;
    qint64 unitMs = 0; // the smallest unit shown
};

// "3d 22h", "Ends in 3d 22h", "3d 14h left", "45m 12s": days, hours,
// minutes, seconds in that order, each optional.
std::optional<Relative> parseRelative(const QString &text)
{
    QString rest = text.simplified();
    rest.remove(QRegularExpression(QStringLiteral("^Ends in\\s*"), QRegularExpression::CaseInsensitiveOption));
    rest.remove(QRegularExpression(QStringLiteral("\\s*left$"), QRegularExpression::CaseInsensitiveOption));
    static const QRegularExpression pattern(QStringLiteral("^(?:(\\d+)d)?\\s*(?:(\\d+)h)?\\s*(?:(\\d+)m)?\\s*(?:(\\d+)s)?$"));
    const QRegularExpressionMatch match = pattern.match(rest);
    if (rest.isEmpty() || !match.hasMatch())
        return std::nullopt;
    const qint64 units[] = {24 * 3600 * 1000LL, 3600 * 1000LL, kMinuteMs, 1000};
    Relative relative;
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        if (match.captured(i + 1).isEmpty())
            continue;
        any = true;
        relative.ms += match.captured(i + 1).toLongLong() * units[i];
        relative.unitMs = units[i];
    }
    if (!any)
        return std::nullopt;
    return relative;
}

struct WeekdayTime {
    int dayOfWeek = 0; // 1 = Monday ... 7 = Sunday
    QTime time;
};

// "Friday, 06:28 PM", "Thu, 01:11 PM", "(Thu, 01:11 PM)".
std::optional<WeekdayTime> parseWeekdayTime(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\(?([A-Za-z]+),\\s*(\\d{1,2}):(\\d{2})\\s*([AaPp][Mm])\\)?$"));
    const QRegularExpressionMatch match = pattern.match(text.simplified());
    if (!match.hasMatch())
        return std::nullopt;
    const QString day = match.captured(1).toLower();
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    WeekdayTime result;
    for (int d = 1; d <= 7; ++d) {
        if (day == english.dayName(d, QLocale::LongFormat).toLower() || day == english.dayName(d, QLocale::ShortFormat).toLower())
            result.dayOfWeek = d;
    }
    int hour = match.captured(2).toInt();
    const int minute = match.captured(3).toInt();
    if (result.dayOfWeek == 0 || hour < 1 || hour > 12 || minute > 59)
        return std::nullopt;
    const bool pm = match.captured(4).toLower() == QLatin1String("pm");
    hour = hour % 12 + (pm ? 12 : 0);
    result.time = QTime(hour, minute);
    return result;
}

} // namespace

std::optional<AuctionEnd> auctionEnd(const QString &relative, const QString &weekdayTime, const QDateTime &now)
{
    const std::optional<Relative> left = parseRelative(relative);
    if (!left)
        return std::nullopt;
    const QDateTime from = now.addMSecs(left->ms - kMinuteMs);
    const QDateTime to = now.addMSecs(left->ms + left->unitMs + kMinuteMs);
    const std::optional<WeekdayTime> shown = parseWeekdayTime(weekdayTime);
    if (shown) {
        const QDateTime local = now.toLocalTime();
        std::optional<QDateTime> found;
        for (int d = 0; d <= 14; ++d) {
            const QDate date = local.date().addDays(d);
            if (date.dayOfWeek() != shown->dayOfWeek)
                continue;
            const QDateTime candidate(date, shown->time, QTimeZone::LocalTime);
            if (candidate >= from && candidate <= to) {
                if (found)
                    return std::nullopt; // two fit: not decided by the page
                found = candidate;
            }
        }
        if (!found)
            return std::nullopt;
        return AuctionEnd{found->toUTC(), false};
    }
    if (left->unitMs == 1000)
        return AuctionEnd{now.addMSecs(left->ms).toUTC(), true};
    return std::nullopt;
}

QString timeLeftText(const QDateTime &end, bool seconds, const QDateTime &now)
{
    const qint64 ms = now.msecsTo(end);
    if (ms <= 0)
        return QStringLiteral("ENDED");
    const qint64 totalSeconds = ms / 1000;
    const qint64 days = totalSeconds / 86400;
    const qint64 hours = totalSeconds % 86400 / 3600;
    const qint64 minutes = totalSeconds % 3600 / 60;
    const qint64 secs = totalSeconds % 60;
    if (days > 0)
        return QStringLiteral("%1d %2h %3m left").arg(days).arg(hours).arg(minutes);
    if (hours > 0)
        return QStringLiteral("%1h %2m left").arg(hours).arg(minutes);
    if (seconds)
        return QStringLiteral("%1m %2s left").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1m left").arg(minutes);
}

} // namespace app::lookup
