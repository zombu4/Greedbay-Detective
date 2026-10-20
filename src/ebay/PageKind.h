#pragma once

#include <QString>

#include <optional>

namespace app::ebay {

// The eBay page types the app reads. Each has its own extractor
// (src/browser/extractors/) and its own key elements that prove the loaded
// page is the expected one.
enum class PageKind {
    ActiveSearch, // /sch/i.html?_nkw=<query>
    SoldSearch,   // /sch/i.html?_nkw=<query>&LH_Sold=1&LH_Complete=1
    Item,         // /itm/<item number>
    Research,     // /sh/research?...&tabName=SOLD (Seller Hub Product Research)
    Feedback,     // /fdbk/feedback_profile/<seller>?... (classic feedback view; #negative or #neutral)
};

// The names used in the browser worker protocol and in extractor results.
QString pageKindName(PageKind kind);
std::optional<PageKind> pageKindFromName(const QString &name);

} // namespace app::ebay
