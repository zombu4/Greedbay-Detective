#pragma once

#include "lookup/LookupStore.h"
#include "lookup/Sellers.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace app::lookup {

// The app's own exports (Settings "Export data", Results "Save as JSON";
// DESIGN 8: lookups as CSV and JSON). Money in cents, times UTC ISO-8601,
// as the database holds them.

// One line per lookup (newest first, as given): id, checked_utc, input,
// item_id, title, brand, model, status, listing_total_cents, sold_count,
// sold_median_cents, sold_confidence, shipping_flagged, for_sale_count,
// cheaper_for_sale, cheapest_for_sale_cents. RFC 4180: fields with a comma,
// a quote or a line break are quoted, quotes doubled; lines end in CRLF. A
// field starting with = + - or @ gets a leading apostrophe, so a
// spreadsheet never reads a seller's title as a formula.
QByteArray lookupsCsv(const QList<LookupSummary> &lookups);

// One lookup with its listing (with the seller's id and rating), identity,
// searches and every row (source, the card's seller and rating, item number,
// title, total, eBay condition, date, bucket, reasons, counted).
QJsonObject lookupJson(const SavedLookup &lookup);

// The user's marked sellers (local only; Settings "Export data"): one line
// each, seller_id, reason (its label), note, marked_on. Same CSV rules as
// lookupsCsv.
QByteArray markedSellersCsv(const QList<SellerMark> &marks);

// {"format": 1, "lookups": [lookupJson(...), ...]}, indented.
QByteArray lookupsJson(const QList<SavedLookup> &lookups);

} // namespace app::lookup
