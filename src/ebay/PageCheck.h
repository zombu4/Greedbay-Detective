#pragma once

#include <QString>
#include <QUrl>

namespace app::ebay {

// Decisions made from a URL alone, before or instead of looking at the page.
//
// Observed behavior these rules rest on (2026-10-18):
//   - signed out, https://www.ebay.com/mye/myebay/summary and /sh/research
//     answer 302 with Location https://signin.ebay.com/ws/eBayISAPI.dll?SignIn&...
//     &ru=<the requested URL>
//   - an account without Seller Hub is sent from /sh/research to /sh/landing
enum class UrlClass {
    SignIn,          // host signin.ebay.com: eBay wants the user to sign in
    ResearchLanding, // www.ebay.com/sh/landing: no Product Research access
    AccountPage,     // www.ebay.com/mye/myebay...: only reachable while signed in
    Other,
};

UrlClass classifyUrl(const QUrl &url);

// The page the app opens to sign in. Signed out, eBay redirects it to its own
// sign-in page and returns to it after a successful sign-in (ru= parameter).
QUrl signInStartUrl();

// True when the browser may show `url` as the main page: https on ebay.com or
// one of its subdomains. With allowLocalTestPages (test builds only), also
// file: URLs and http:// or https://127.0.0.1 (the tests' local servers).
bool isAllowedMainFrameUrl(const QUrl &url, bool allowLocalTestPages);

// The item number in https://www.ebay.com/itm/<digits>[/...], or an empty
// string when the URL is not an eBay item URL.
QString itemIdFromUrl(const QUrl &url);

// The URL as it may appear in a log, for pages eBay or the user navigated to:
// scheme and host, plus the path only for the page types the app knows
// (/sch/, /itm/, /sh/, /mye/, /ws/); any other path becomes "/..." because it
// may hold a name (for example /usr/<user> or /str/<store>). Query, fragment
// and user info are always dropped (sign-in pages can carry tokens in the
// query). URLs the app builds itself are logged in full.
QString urlForLog(const QUrl &url);

} // namespace app::ebay
