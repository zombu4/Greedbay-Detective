#include "browser/BrowserPage.h"

#include "ebay/PageCheck.h"

namespace app::browser {

BrowserPage::BrowserPage(QWebEngineProfile *profile, bool allowLocalTestPages, QObject *parent)
    : QWebEnginePage(profile, parent), m_allowLocalTestPages(allowLocalTestPages)
{
}

bool BrowserPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    Q_UNUSED(type);
    if (!isMainFrame)
        return true;
    if (ebay::isAllowedMainFrameUrl(url, m_allowLocalTestPages))
        return true;
    emit navigationBlocked(url);
    return false;
}

QStringList BrowserPage::chooseFiles(FileSelectionMode mode, const QStringList &oldFiles,
                                     const QStringList &acceptedMimeTypes)
{
    Q_UNUSED(mode);
    Q_UNUSED(oldFiles);
    Q_UNUSED(acceptedMimeTypes);
    return {};
}

void BrowserPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                           int lineNumber, const QString &sourceID)
{
    Q_UNUSED(level);
    Q_UNUSED(message);
    Q_UNUSED(lineNumber);
    Q_UNUSED(sourceID);
}

} // namespace app::browser
