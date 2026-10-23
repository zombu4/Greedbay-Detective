#pragma once

#include <QString>
#include <QUrl>

namespace app::ebay {

// The eBay addresses a lookup opens, in the forms recorded in recon
// sections 1-4 (ebay.com only):
//   active search   https://www.ebay.com/sch/i.html?_nkw=<query>&_ipg=240[&_pgn=N]
//   sold search     the same plus LH_Sold=1&LH_Complete=1
//   Product Research https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=<query>
//                   &dayRange=<days>&tabName=SOLD&limit=50&offset=<N>
//   item page       https://www.ebay.com/itm/<item number>?nordt=true&orig_cvip=true&rt=nc
//                   (architect, 2026-10-22: an ended listing then shows itself
//                   instead of redirecting to a similar or catalog page; every
//                   item address the app opens or loads is this one)
// In _nkw a space is "+", as in eBay's own pagination links; in keywords it is
// "%20", as in the Product Research address eBay uses.

// Results per search page the app asks for (the most eBay offers, recon 1).
constexpr int kSearchPageSize = 240;
// Rows per Product Research page (recon 4).
constexpr int kResearchPageSize = 50;

// `page` starts at 1.
QUrl activeSearchUrl(const QString &query, int page);
QUrl soldSearchUrl(const QString &query, int page);
QUrl researchUrl(const QString &query, int dayRange, int offset);
QUrl itemUrl(const QString &itemId);

// The item number in what the user pasted: an https eBay item address
// (https://www.ebay.com/itm/<digits>, optionally followed by /..., a query or a
// fragment; see itemIdFromUrl) or a bare item number of 12 digits (the length of
// every item number seen in recon). An empty string for anything else.
// Surrounding white space is ignored.
QString itemIdFromInput(const QString &input);

} // namespace app::ebay
