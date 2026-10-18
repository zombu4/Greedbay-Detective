#pragma once

#include <QUrl>
#include <QWebEnginePage>

namespace app::browser {

// The one page of a browser session, with the session's limits:
//   - the main page may only show https pages on ebay.com and its subdomains
//     (see ebay::isAllowedMainFrameUrl); other main-page navigations,
//     including redirects, are refused and reported by navigationBlocked().
//     Frames inside a page (for example a security check's frame) are not
//     restricted, as in any browser.
//   - file pickers are refused (the page never gets a local file).
//   - the page's JavaScript console messages are dropped: Qt would otherwise
//     pass them to the log, and logs never hold page text.
class BrowserPage : public QWebEnginePage {
    Q_OBJECT
public:
    BrowserPage(QWebEngineProfile *profile, bool allowLocalTestPages, QObject *parent = nullptr);

signals:
    void navigationBlocked(const QUrl &url);

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
    QStringList chooseFiles(FileSelectionMode mode, const QStringList &oldFiles,
                            const QStringList &acceptedMimeTypes) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber,
                                  const QString &sourceID) override;

private:
    bool m_allowLocalTestPages;
};

} // namespace app::browser
