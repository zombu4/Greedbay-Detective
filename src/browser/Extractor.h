#pragma once

#include "ebay/PageKind.h"

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <functional>

class QWebEnginePage;

namespace app::browser {

// Runs a page extractor (extractors/*.js, built into the binary as Qt
// resources) in a loaded page and checks what it returns.
//
// The result is one JSON object:
//   {"page":"expected","data":{...}}            the expected page and what was read
//   {"page":"sign_in","reason":"..."}           eBay's sign-in page (from the URL)
//   {"page":"no_access","reason":"..."}         Product Research redirected to /sh/landing (from the URL)
//   {"page":"error_page","reason":"..."}        eBay's error page
//   {"page":"unexpected","reason":"..."}        the key elements of the expected page are missing
//                                               (a security check, a changed page, a failed load, ...)
//   {"page":"invalid","reason":"..."}           the script did not return a valid result
class Extractor {
public:
    // Classifies and reads the page currently shown in `page` as `kind`.
    // `done` is called exactly once (Qt guarantees the runJavaScript callback,
    // possibly while the page is being destroyed).
    static void run(QWebEnginePage *page, ebay::PageKind kind, std::function<void(const QJsonObject &)> done);

    // The complete script for `kind`, with `url` (the page's URL) as input.
    static QString script(ebay::PageKind kind, const QUrl &url);

    // Checks a script result (a JSON string) and turns it into the result
    // object described above.
    static QJsonObject parseResult(const QVariant &value);
};

} // namespace app::browser
