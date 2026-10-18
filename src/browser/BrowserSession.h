#pragma once

#include "core/WorkFolder.h"
#include "ebay/PageKind.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QUrl>

#include <memory>

class QTimer;
class QWebEngineProfile;

namespace app::browser {

class BrowserPage;
class BrowserWindow;

struct SessionOptions {
    // Test builds only: lets the page show file: URLs and http://127.0.0.1.
    bool allowLocalTestPages = false;
};

// One browser session: the Qt WebEngine profile, its single page and the
// window that shows it when the user has to act. It lives inside the browser
// worker process and is created when the first fetch or sign-in of a session
// is requested; destroying it tears everything down (window and page, then
// the profile, then the session lock).
//
// Storage: everything the profile writes goes to the work folder:
//   webengine/storage    cookies, local storage and the other profile data
//   webengine/cache      HTTP cache (at most kHttpCacheMaxBytes)
//   webengine/downloads  download path; every download is refused
//
// One operation at a time (fetch or signIn); each ends with exactly one
// done() signal carrying the reply for the worker protocol (see
// core/BrowserProtocol.h).
//
// Fetch: loads the URL with the window hidden and runs the page's extractor.
// If the page is not the expected one (eBay's error page, the sign-in page,
// the Seller Hub start page, a security check or anything else without the
// page's key elements), the fetch stops: the window shows that page with a
// plain message, and the reply is sent when the user closes the window.
// Nothing is retried and no check is ever solved or skipped by the app.
//
// Sign-in: opens signInStartUrl() (the My eBay summary). Signed out, eBay
// redirects it to its own sign-in page, which the window then shows; the user
// signs in there. The session never reads, fills in or stores credentials:
// it only watches which page eBay shows. Signed in = the My eBay page is
// shown and it is not eBay's error page. Closing the window cancels.
//
// Pacing: one page at a time, and at least kPoliteDelayMs between the end of
// one page load the app started and the start of the next.
class BrowserSession : public QObject {
    Q_OBJECT
public:
    // 3 seconds between page loads. A lookup reads about ten pages (four page
    // types plus their result pages), so this adds about half a minute and
    // keeps the app's pace near that of a person paging through results. The
    // number is a project choice, not a limit published by eBay.
    static constexpr int kPoliteDelayMs = 3000;
    // A page that has not finished loading after 60 s fails the fetch.
    static constexpr int kLoadTimeoutMs = 60000;
    static constexpr int kHttpCacheMaxBytes = 64 * 1024 * 1024;
    // The profile's storage name. Every path of the profile is set
    // explicitly, so the name never decides where files go.
    static constexpr char kStorageName[] = "ebay";

    // Takes the session lock and creates the profile, page and (hidden)
    // window. Returns nullptr and sets *error when the lock is held by
    // another session or Qt WebEngine did not accept the paths.
    static std::unique_ptr<BrowserSession> create(const WorkFolder &folder, SessionOptions options,
                                                  QString *error);
    ~BrowserSession() override;

    BrowserSession(const BrowserSession &) = delete;
    BrowserSession &operator=(const BrowserSession &) = delete;

    bool isBusy() const { return m_op != Op::None; }

    void fetch(const QUrl &url, ebay::PageKind kind);
    void signIn();
    // Ends the current operation with a "failed: stopped" reply.
    void cancel();

signals:
    void done(const QJsonObject &reply);

private:
    enum class Op { None, Fetch, SignIn };

    BrowserSession(const WorkFolder &folder, SessionOptions options);
    bool init(QString *error);

    void scheduleLoad(const QUrl &url);
    void startLoad();
    void onLoadFinished(bool ok);
    void onLoadTimeout();
    void onExtracted(const QJsonObject &result, bool loadOk, quint64 serial);
    void checkSignedIn();
    void onWindowClosed();
    void onNavigationBlocked(const QUrl &url);
    void onRendererTerminated(int status, int exitCode);
    void finish(const QJsonObject &reply);
    void showUnexpected(const QJsonObject &reply, const QString &message);

    WorkFolder m_folder;
    SessionOptions m_options;
    QLockFile m_lock;
    QWebEngineProfile *m_profile = nullptr;
    BrowserPage *m_page = nullptr;
    BrowserWindow *m_window = nullptr;
    QTimer *m_delayTimer = nullptr;
    QTimer *m_loadTimer = nullptr;

    Op m_op = Op::None;
    // Increases with every finished operation; callbacks of an earlier
    // operation (for example a late extractor result) are ignored.
    quint64 m_serial = 0;
    ebay::PageKind m_kind = ebay::PageKind::ActiveSearch;
    QUrl m_url;
    bool m_loading = false;
    bool m_waitingForClose = false;
    bool m_signInShown = false;
    QJsonObject m_replyAfterClose;
    QElapsedTimer m_sinceLastLoad;
    QElapsedTimer m_loadClock;
};

} // namespace app::browser
