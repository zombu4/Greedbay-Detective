#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace app::lookup {

// When an auction ends, from what eBay shows (recon 2026-10-19): the item
// page has "Ends in 3d 22h" (span.ux-timer__text) and "Friday, 06:28 PM"
// (span.ux-timer__time-left); a search card has "3d 14h left"
// (span.s-card__time-left) and "(Thu, 01:11 PM)" (span.s-card__time-end).
// Neither has a date, a year or a time zone, and the page carries no
// timestamp.
//
// The weekday and time are taken as local time of this computer (seen once:
// the item page's JSON-LD start 22:28:43Z = 18:28 in New York, shown as
// "06:28 PM" on a Mac in that zone). They are used only when they fall
// inside the window the relative text gives (from now + the shown time to
// now + the shown time + its smallest unit, a minute of slack either side):
// two values of the page agreeing, nothing assumed beyond that. Without the
// weekday, a relative time down to seconds gives the end by itself.
// Anything else: no end time (shown as "-").
struct AuctionEnd {
    QDateTime end;        // UTC
    bool seconds = false; // known to the second (else to the minute)
};

std::optional<AuctionEnd> auctionEnd(const QString &relative, const QString &weekdayTime, const QDateTime &now);

// The countdown shown for an auction: "2d 5h 12m left", "5h 12m left",
// "12m 05s left" in the last hour when the end is known to the second
// ("12m left" otherwise), "ENDED" from the end on.
QString timeLeftText(const QDateTime &end, bool seconds, const QDateTime &now);

} // namespace app::lookup
