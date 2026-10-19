#pragma once

#include "core/WorkFolder.h"
#include "ebay/PageKind.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <memory>

class QLockFile;
class QTimer;
class QWebEngineLoadingInfo;
class QWebEngineProfile;

namespace app::browser {

class BrowserPage;
class BrowserWindow;

struct SessionOptions {
    // Test builds only: lets the page show file: URLs and http(s)://127.0.0.1.
    bool allowLocalTestPages = false;
};

// One browser session: the Qt WebEngine profile, its single page and the
// window that shows it when the user has to act. It lives inside the browser
// worker process and is created when the first fetch or sign-in of a session
// is requested; destroying it deletes the window and the page, then the
// profile.
//
// Storage: everything the profile writes goes to the work folder:
//   webengine/storage    cookies, local storage and the other profile data
//   webengine/cache      HTTP cache (at most kHttpCacheMaxBytes)
//   webengine/downloads  download path; every download is refused
// The session lock (WebEngineData::sessionLockPath) must be held by the
// caller for the whole life of the worker process, so that sign-out cannot
// run while Chromium may still write; create() takes it if it is not held.
//
// One operation at a time (fetch or signIn); each ends with exactly one
// done() signal carrying the reply for the worker protocol (see
// core/BrowserProtocol.h). Every automatic step has a time limit; only steps
// that wait for the user (an unexpected page or sign-in shown in the window)
// wait until the user closes the window.
//
// Fetch: loads the URL with the window hidden. When the load succeeded, the
// page's extractor runs; while it does not find the page's key elements it is
// run again every kSettleStepMs for up to kSettleMs (pages that build their
// content after loading). The fetch then stops if the page is not the expected
// one (eBay's error page, the sign-in page, the Seller Hub start page, a
// security check, a failed or incomplete load, anything without the page's
// key elements): the window shows that page with a plain message, and the
// reply is sent when the user closes the window. Nothing is reloaded or
// retried and no check is ever solved or skipped by the app.
//
// Sign-in: opens signInStartUrl() (the My eBay summary). Signed out, eBay
// redirects it to its own sign-in page, which the window then shows; the user
// signs in there. The session never reads, fills in or stores credentials:
// it only watches which page eBay shows. Signed in = the My eBay page loaded
// successfully, it is not eBay's error page and it has no password field.
// Closing the window cancels.
//
// Pacing: one page at a time, and at least kPoliteDelayMs between the end of
// any page load (the app's or the user's, finished or stopped) and the start
// of the next page load the app makes; the first load of a session waits
// kPoliteDelayMs from the session's start.
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
    // After a successful load, how long the key elements may take to appear.
    static constexpr int kSettleMs = 10000;
    static constexpr int kSettleStepMs = 500;
    // One extractor run that has not answered after 15 s fails the request.
    static constexpr int kScriptTimeoutMs = 15000;
    static constexpr int kHttpCacheMaxBytes = 64 * 1024 * 1024;
    // The profile's storage name. Every path of the profile is set
    // explicitly, so the name never decides where files go.
    static constexpr char kStorageName[] = "ebay";

    // Takes `sessionLock` if it is not held yet, then creates the profile,
    // page and (hidden) window. Returns nullptr and sets *error when the lock
    // is held by another process or Qt WebEngine did not accept the paths.
    static std::unique_ptr<BrowserSession> create(const WorkFolder &folder, SessionOptions options,
                                                  QLockFile *sessionLock, QString *error);
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
    // Idle: no page load of the app runs. Requested: load() was called and
    // the page has not reported that load's start yet (end reports of an
    // earlier navigation arriving now are ignored). Running: the app's load
    // runs.
    enum class Load { Idle, Requested, Running };

    BrowserSession(const WorkFolder &folder, SessionOptions options);
    bool init(QString *error);

    void scheduleLoad(const QUrl &url);
    void startLoad();
    void onLoadingChanged(const QWebEngineLoadingInfo &info);
    void onLoadTimeout();
    void runExtractor(bool loadOk);
    void onExtracted(const QJsonObject &result, bool loadOk);
    void checkSignedIn(bool loadOk);
    void onWindowClosed();
    void onNavigationBlocked(const QUrl &url);
    void onRendererTerminated(int status, int exitCode);
    void onScriptTimeout();
    void stopLoading();
    void stopTimers();
    void showWindow(const QString &message);
    void hideWindow();
    void showUnexpected(const QJsonObject &reply, const QString &message);
    void finish(const QJsonObject &reply);

    WorkFolder m_folder;
    SessionOptions m_options;
    QWebEngineProfile *m_profile = nullptr;
    BrowserPage *m_page = nullptr;
    BrowserWindow *m_window = nullptr;
    QTimer *m_delayTimer = nullptr;
    QTimer *m_loadTimer = nullptr;
    QTimer *m_settleTimer = nullptr;
    QTimer *m_scriptTimer = nullptr;

    Op m_op = Op::None;
    Load m_load = Load::Idle;
    // Increases with every finished operation and every script run; a
    // callback that carries an older value (a late extractor result, a
    // sign-in check of an earlier page) is ignored.
    quint64 m_serial = 0;
    ebay::PageKind m_kind = ebay::PageKind::ActiveSearch;
    QUrl m_url;
    bool m_lastLoadOk = false;
    // Set while stopLoading() stops the page: the load events this causes
    // are not results of anything.
    bool m_stopping = false;
    bool m_waitingForClose = false;
    bool m_signInShown = false;
    QJsonObject m_replyAfterClose;
    QElapsedTimer m_sinceLastLoad;
    QElapsedTimer m_loadClock;
    QElapsedTimer m_settleClock;
};

} // namespace app::browser
