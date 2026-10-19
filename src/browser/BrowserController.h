#pragma once

#include "browser/BrowserCatalog.h"
#include "core/WorkFolder.h"
#include "ebay/PageKind.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>

class QTimer;

namespace app::browser {

class CdpConnection;

struct BrowserControllerOptions {
    // Random wait between page loads, in [min, max] (Settings; owner decision
    // 3-6 s).
    int pageDelayMinMs = 3000;
    int pageDelayMaxMs = 6000;
    // Tests only: fetch may also open http://127.0.0.1 and file: pages.
    bool allowLocalTestPages = false;
};

// One browser session in the user's installed browser: a separate instance
// of that browser with its own profile folder in the work folder
// (<work>/browser/<id>/, see core/BrowserProfiles), started by start() and
// closed by stop(). The app never touches the user's normal browser profile.
//
// The browser is driven over the DevTools protocol (CdpConnection). It runs
// as a normal browser: no automation switches, so pages see no automation
// marker (navigator.webdriver stays false). Its window is minimized while the
// app reads pages and brought to the front when the user has to act.
//
// One operation at a time, each ending with exactly one done() reply (the
// reply objects of the old worker protocol: {"status":"ok","result":{...}},
// {"status":"unexpected_page","page":...,"reason":...},
// {"status":"ok","signedIn":bool}, {"status":"failed","error":...}).
//
// fetch(): loads the URL and runs the page's extractor (Extractor). If the page
// is not the expected one (eBay's error page, the sign-in page, the Seller Hub
// start page, a security check, a failed load, anything without the page's key
// elements), the browser window is shown, needsUser() tells the user what to do,
// and the reply is sent when the user calls continueAfterUser(). Nothing is
// reloaded or retried, and no check is ever solved or skipped by the app.
//
// signIn(): opens the eBay homepage in the shown window; the user clicks
// "Sign in" there himself and signs in on eBay's own page, then calls
// continueAfterUser(). The app then opens the My eBay summary once: signed in
// = that page loaded successfully, is not eBay's error page and asks for no
// password (signed out, eBay redirects it to its sign-in page). The app never
// reads, fills in or stores credentials.
//
// Pacing: one page at a time, and a random wait in [pageDelayMinMs,
// pageDelayMaxMs] before every page load the app makes, counted from the end
// of the previous load (or from the session's start for the first one).
class BrowserController : public QObject {
    Q_OBJECT
public:
    static constexpr int kStartTimeoutMs = 30000;
    static constexpr int kLoadTimeoutMs = 60000;
    static constexpr int kSettleMs = 10000;
    static constexpr int kSettleStepMs = 500;
    static constexpr int kScriptTimeoutMs = 15000;
    static constexpr int kCloseTimeoutMs = 15000;

    BrowserController(WorkFolder folder, InstalledBrowser browser, BrowserControllerOptions options,
                      QObject *parent = nullptr);
    ~BrowserController() override;

    BrowserController(const BrowserController &) = delete;
    BrowserController &operator=(const BrowserController &) = delete;

    // Takes the profile lock and starts the browser; ready() or failed()
    // follows. A controller runs one session: after finished(), make a new
    // one. Returns false (and sets *error) when the lock is held or the
    // browser cannot be started.
    bool start(QString *error);

    void fetch(const QUrl &url, ebay::PageKind kind);
    void signIn();
    // The user has done what needsUser() asked for.
    void continueAfterUser();
    // Ends the current operation with a "failed: stopped" reply.
    void cancel();
    // Closes the browser (Browser.close; killed after kCloseTimeoutMs) and
    // releases the profile lock; finished() follows.
    void stop();

    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_op != Op::None; }
    bool isRunning() const;
    qint64 processId() const;
    const InstalledBrowser &browser() const { return m_browser; }

signals:
    void ready();
    void done(const QJsonObject &reply);
    // The browser window is shown; `message` says what the user should do
    // there, then press Continue (continueAfterUser()).
    void needsUser(const QString &message);
    // Something ended the session (start failed, the browser or its tab was
    // closed, the protocol broke). finished() follows.
    void failed(const QString &error);
    // The browser process has exited; `normal` after stop() and a clean exit.
    void finished(bool normal);

private:
    // SignInOpening: the homepage loads; SignInWaiting: the user signs in;
    // SignInChecking: the My eBay page loads to see whether he did.
    enum class Op { None, Fetch, SignInOpening, SignInWaiting, SignInChecking };
    using Callback = std::function<void(const QJsonObject &result, const QJsonObject &error)>;

    void command(const QString &method, const QJsonObject &params, Callback callback, bool onPage = true);
    void onResponse(int id, const QJsonObject &result, const QJsonObject &error);
    void onEvent(const QString &method, const QJsonObject &params, const QString &sessionId);
    void onBrowserFinished(int exitCode, bool crashed);
    void attachToFirstPage(int attemptsLeft);
    void setWindowState(const char *state);

    void scheduleLoad(const QUrl &url);
    void startLoad();
    void endLoad(bool ok);
    void runExtractor();
    void onExtracted(const QJsonObject &result);
    void checkSignedIn(bool loadOk);
    void waitForUser(const QJsonObject &reply, const QString &message);
    void finish(const QJsonObject &reply);
    void fail(const QString &error);
    void stopTimers();

    WorkFolder m_folder;
    InstalledBrowser m_browser;
    BrowserControllerOptions m_options;
    QLockFile m_lock;
    CdpConnection *m_cdp = nullptr;
    std::map<int, Callback> m_callbacks;
    QString m_targetId;
    QString m_sessionId;
    int m_windowId = -1;
    QString m_mainFrameId;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_failed = false;

    QTimer *m_startTimer = nullptr;
    QTimer *m_delayTimer = nullptr;
    QTimer *m_loadTimer = nullptr;
    QTimer *m_settleTimer = nullptr;
    QTimer *m_scriptTimer = nullptr;

    Op m_op = Op::None;
    // Increases with every finished operation and every page load; a late
    // callback of an earlier load or operation is ignored.
    quint64 m_serial = 0;
    ebay::PageKind m_kind = ebay::PageKind::ActiveSearch;
    QUrl m_url;
    bool m_loading = false;
    QString m_loaderId;
    struct Document {
        int status = 0;
        QUrl url;
    };
    // Main-frame document responses of the current load, by loader id.
    QHash<QString, Document> m_documents;
    QString m_navigationError;
    int m_httpStatus = 0;
    QUrl m_documentUrl;
    bool m_lastLoadOk = false;
    bool m_waitingForUser = false;
    QJsonObject m_replyAfterUser;
    QElapsedTimer m_sinceLastLoad;
    QElapsedTimer m_loadClock;
    QElapsedTimer m_settleClock;
};

} // namespace app::browser
