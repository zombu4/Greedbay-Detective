#include "ebay/PageKind.h"

namespace app::ebay {

QString pageKindName(PageKind kind)
{
    switch (kind) {
    case PageKind::ActiveSearch:
        return QStringLiteral("activeSearch");
    case PageKind::SoldSearch:
        return QStringLiteral("soldSearch");
    case PageKind::Item:
        return QStringLiteral("item");
    case PageKind::Research:
        return QStringLiteral("research");
    }
    return {};
}

std::optional<PageKind> pageKindFromName(const QString &name)
{
    for (PageKind kind : {PageKind::ActiveSearch, PageKind::SoldSearch, PageKind::Item, PageKind::Research}) {
        if (pageKindName(kind) == name)
            return kind;
    }
    return std::nullopt;
}

} // namespace app::ebay
