#include "browser/BrowserIdentity.h"

#include <QRegularExpression>

namespace app::browser {

QString browserUserAgent(const QString &engineUserAgent)
{
    // " QtWebEngine/6.140.0" between "(KHTML, like Gecko)" and "Chrome/...".
    static const QRegularExpression token(QStringLiteral(" ?QtWebEngine/\\S+"));
    QString userAgent = engineUserAgent;
    userAgent.remove(token);
    return userAgent.simplified();
}

} // namespace app::browser
