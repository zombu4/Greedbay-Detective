#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace app::ui {

// Text forms the screens share. English only for now (ebay.com / USD, owner
// decision Q5).

// "$1,234.56", or "-" when there is no amount.
QString moneyText(const std::optional<qint64> &cents);

// When something happened, in local time, relative to `now`:
// "today 14:05", "yesterday 09:12", or "Oct 12, 2026".
QString whenText(const QDateTime &time, const QDateTime &now);

// "1 listing" / "3 listings" for (count, "listing").
QString countText(qsizetype count, const QString &singular, const QString &plural);

} // namespace app::ui
