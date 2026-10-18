#include "ebay/PageCheck.h"

#include <QRegularExpression>

namespace app::ebay {

namespace {

bool isEbayHost(const QString &host)
{
    return host == QLatin1String("ebay.com") || host.endsWith(QLatin1String(".ebay.com"));
}

} // namespace

UrlClass classifyUrl(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https"))
        return UrlClass::Other;
    const QString host = url.host().toLower();
    const QString path = url.path();
    if (host == QLatin1String("signin.ebay.com"))
        return UrlClass::SignIn;
    if (host == QLatin1String("www.ebay.com")) {
        if (path == QLatin1String("/sh/landing") || path.startsWith(QLatin1String("/sh/landing/")))
            return UrlClass::ResearchLanding;
        if (path.startsWith(QLatin1String("/mye/myebay")))
            return UrlClass::AccountPage;
    }
    return UrlClass::Other;
}

QUrl signInStartUrl()
{
    return QUrl(QStringLiteral("https://www.ebay.com/mye/myebay/summary"));
}

bool isAllowedMainFrameUrl(const QUrl &url, bool allowLocalTestPages)
{
    if (url.scheme() == QLatin1String("https") && isEbayHost(url.host().toLower()))
        return true;
    if (allowLocalTestPages) {
        if (url.isLocalFile())
            return true;
        if (url.scheme() == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1"))
            return true;
    }
    return false;
}

QString itemIdFromUrl(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https") || !isEbayHost(url.host().toLower()))
        return {};
    static const QRegularExpression pattern(QStringLiteral("^/itm/(\\d+)(?:/|$)"));
    const QRegularExpressionMatch match = pattern.match(url.path());
    return match.hasMatch() ? match.captured(1) : QString();
}

QString urlForLog(const QUrl &url)
{
    return url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
}

} // namespace app::ebay
