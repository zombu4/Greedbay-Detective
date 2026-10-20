#pragma once

#include "ebay/PageKind.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <optional>

namespace app::browser {

// The page extractors (extractors/*.js, built into the binary as Qt
// resources): builds the script that runs in a loaded page and checks what it
// returns. The browser client (BrowserController) runs the script in the
// page through the browser's DevTools protocol.
//
// The result is one JSON object:
//   {"page":"expected","data":{...}}            the expected page and what was read
//   {"page":"sign_in","reason":"..."}           eBay's sign-in page (from the URL)
//   {"page":"no_access","reason":"..."}         Product Research redirected to /sh/landing (from the URL)
//   {"page":"error_page","reason":"..."}        eBay's error page
//   {"page":"ship_to","reason":"..."}           the key elements are missing and eBay's
//                                               "Are you shipping to ...?" prompt is shown
//   {"page":"unexpected","reason":"..."}        the key elements of the expected page are missing
//                                               (a security check, a changed page, a failed load, ...)
//   {"page":"invalid","reason":"..."}           the script did not return a valid result
class Extractor {
public:
    // The outcome decided from the page's URL alone, before any script runs:
    // eBay's sign-in page (any kind), or the Seller Hub start page when
    // Product Research was asked for. std::nullopt when the page must be read.
    static std::optional<QJsonObject> outcomeFromUrl(const QUrl &url, ebay::PageKind kind);

    // The complete script for `kind`, with `url` (the page's URL) as input.
    // It evaluates to a promise of a JSON string (run it with awaitPromise).
    static QString script(ebay::PageKind kind, const QUrl &url);

    // Checks a script result (the JSON string the script evaluated to; an
    // undefined or non-string result arrives as a null QJsonValue) and turns
    // it into the result object described above.
    static QJsonObject parseResult(const QJsonValue &value);
};

} // namespace app::browser
