#include "browser/BrowserSession.h"

#include "browser/BrowserPage.h"
#include "browser/BrowserWindow.h"
#include "browser/Extractor.h"
#include "core/BrowserProtocol.h"
#include "ebay/PageCheck.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QLoggingCategory>
#include <QMap>
#include <QTimer>
#include <QWebEngineDownloadRequest>
#include <QWebEngineLoadingInfo>
#include <QWebEngineNewWindowRequest>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineProfileBuilder>
#include <QWebEngineScript>

#include <algorithm>

namespace app::browser {

Q_STATIC_LOGGING_CATEGORY(lcSession, "browser.session")

namespace {

namespace protocol = browserprotocol;

const QString kSignInMessage = QStringLiteral(
    "Sign in to eBay in this window. The app never sees or stores your password; it only checks "
    "afterwards whether eBay shows your account page. Close this window to cancel.");

// Run on the My eBay page during sign-in: is it eBay's error page, and does
// it ask for a password (a sign-in or check form)?
const QString kSignedInCheck = QStringLiteral(
    "JSON.stringify({errorPage: document.title === 'Error Page | eBay'"
    " || document.querySelector('.error-header #error-info') !== null,"
    " passwordField: document.querySelector('input[type=password]') !== null})");

QString messageFor(const QString &page, bool loadOk)
{
    if (page == QLatin1String("error_page"))
        return QStringLiteral("eBay showed an error page instead of the expected page, so nothing was read. "
                              "Close this window to continue.");
    if (page == QLatin1String("sign_in"))
        return QStringLiteral("eBay asks you to sign in. You can sign in here; the app never sees your "
                              "password. Close this window when you are done; the app does not reload the "
                              "page by itself.");
    if (page == QLatin1String("no_access"))
        return QStringLiteral("eBay opened the Seller Hub start page instead of Product Research: this "
                              "account has no Product Research access. Prices from sold listings will be "
                              "marked unconfirmed. Close this window to continue.");
    if (!loadOk)
        return QStringLiteral("The page could not be loaded completely, so nothing was read. Close this "
                              "window to continue.");
    return QStringLiteral("This is not the page the app expected (it may be a security check, or eBay "
                          "changed the page), so nothing was read. The app does not solve or skip checks; "
                          "you can complete it here yourself. Close this window to continue; the app does "
                          "not reload the page by itself.");
}

QJsonObject statusReply(const char *status)
{
    return QJsonObject{{QStringLiteral("status"), QString::fromLatin1(status)}};
}

QJsonObject failedReply(const QString &error)
{
    QJsonObject reply = statusReply(protocol::kStatusFailed);
    reply.insert(QStringLiteral("error"), error);
    return reply;
}

// Counts of what an extractor read, for the log: record count and the
// names of missing fields with how often each was missing. No page text.
QString logSummary(const QJsonObject &data)
{
    QJsonArray records;
    if (data.value(QStringLiteral("items")).isArray())
        records = data.value(QStringLiteral("items")).toArray();
    else if (data.value(QStringLiteral("rows")).isArray())
        records = data.value(QStringLiteral("rows")).toArray();
    else
        records.append(data);
    if (data.value(QStringLiteral("summary")).isObject())
        records.append(data.value(QStringLiteral("summary")));

    QMap<QString, int> missing;
    for (const QJsonValue &record : std::as_const(records)) {
        const QJsonArray names = record.toObject().value(QStringLiteral("missing")).toArray();
        for (const QJsonValue &name : names)
            ++missing[name.toString()];
    }
    QStringList parts;
    for (auto it = missing.cbegin(); it != missing.cend(); ++it)
        parts << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
    return QStringLiteral("records=%1 missing=[%2]").arg(records.size()).arg(parts.join(QStringLiteral(", ")));
}

} // namespace

BrowserSession::BrowserSession(const WorkFolder &folder, SessionOptions options)
    : m_folder(folder), m_options(options)
{
}

std::unique_ptr<BrowserSession> BrowserSession::create(const WorkFolder &folder, SessionOptions options,
                                                       QLockFile *sessionLock, QString *error)
{
    if (!sessionLock->isLocked() && !sessionLock->tryLock(0)) {
        if (error) {
            *error = sessionLock->error() == QLockFile::LockFailedError
                         ? QStringLiteral("another browser session is using this work folder")
                         : QStringLiteral("cannot create the browser session lock");
        }
        return nullptr;
    }
    std::unique_ptr<BrowserSession> session(new BrowserSession(folder, options));
    if (!session->init(error))
        return nullptr;
    return session;
}

bool BrowserSession::init(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    // Persistent cookies including session cookies (ForcePersistentCookies):
    // the user signs in once and stays signed in across sessions, which end
    // after every lookup. Permissions are never stored (AskEveryTime), and
    // every permission request is denied below.
    QWebEngineProfileBuilder builder;
    builder.setPersistentStoragePath(m_folder.webEngineStorageDir())
        .setCachePath(m_folder.webEngineCacheDir())
        .setHttpCacheType(QWebEngineProfile::DiskHttpCache)
        .setHttpCacheMaximumSize(kHttpCacheMaxBytes)
        .setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies)
        .setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);
    m_profile = builder.createProfile(QString::fromLatin1(kStorageName));
    if (!m_profile)
        return fail(QStringLiteral("Qt WebEngine refused the browser profile (its storage path is in use)"));

    // Qt WebEngine must use exactly the work folder paths; anything else
    // would write outside the work folder.
    if (QDir::cleanPath(m_profile->persistentStoragePath()) != QDir::cleanPath(m_folder.webEngineStorageDir())
        || QDir::cleanPath(m_profile->cachePath()) != QDir::cleanPath(m_folder.webEngineCacheDir()))
        return fail(QStringLiteral("Qt WebEngine did not accept the work folder paths"));
    m_profile->setDownloadPath(m_folder.webEngineDownloadsDir());
    m_profile->setSpellCheckEnabled(false);
    m_profile->setPushServiceEnabled(false);
    connect(m_profile, &QWebEngineProfile::downloadRequested, this, [](QWebEngineDownloadRequest *download) {
        qCInfo(lcSession).noquote() << "download refused:" << ebay::urlForLog(download->url());
        download->cancel();
    });

    m_page = new BrowserPage(m_profile, m_options.allowLocalTestPages);
    connect(m_page, &QWebEnginePage::loadingChanged, this, &BrowserSession::onLoadingChanged);
    connect(m_page, &BrowserPage::navigationBlocked, this, &BrowserSession::onNavigationBlocked);
    connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                onRendererTerminated(static_cast<int>(status), exitCode);
            });
    connect(m_page, &QWebEnginePage::permissionRequested, this, [](QWebEnginePermission permission) {
        permission.deny();
    });
    connect(m_page, &QWebEnginePage::newWindowRequested, this, [this](QWebEngineNewWindowRequest &request) {
        // Pop-ups open in the same page, and only while the user is looking
        // at the window; a page loading hidden cannot replace itself.
        const QUrl url = request.requestedUrl();
        if (!m_window->isVisible())
            return;
        if (!ebay::isAllowedMainFrameUrl(url, m_options.allowLocalTestPages)) {
            onNavigationBlocked(url);
            return;
        }
        m_page->load(url);
    });

    m_window = new BrowserWindow(m_page);
    connect(m_window, &BrowserWindow::closed, this, &BrowserSession::onWindowClosed);

    auto makeTimer = [this](void (BrowserSession::*slot)()) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, slot);
        return timer;
    };
    m_delayTimer = makeTimer(&BrowserSession::startLoad);
    m_loadTimer = makeTimer(&BrowserSession::onLoadTimeout);
    m_scriptTimer = makeTimer(&BrowserSession::onScriptTimeout);
    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    connect(m_settleTimer, &QTimer::timeout, this, [this]() { runExtractor(m_lastLoadOk); });

    qCInfo(lcSession) << "browser session started";
    return true;
}

BrowserSession::~BrowserSession()
{
    // Qt may run a pending runJavaScript callback while the page is being
    // deleted; with the operation ended here, such a callback does nothing.
    m_op = Op::None;
    ++m_serial;
    if (m_delayTimer)
        stopTimers();
    // Order matters: the view and the page go first, the profile last, so the
    // profile outlives every page that uses it.
    if (m_page)
        m_page->disconnect(this);
    if (m_window) {
        m_window->disconnect(this);
        m_window->releasePage();
        delete m_window;
    }
    delete m_page;
    delete m_profile;
    qCInfo(lcSession) << "browser session ended";
}

void BrowserSession::fetch(const QUrl &url, ebay::PageKind kind)
{
    if (isBusy()) {
        emit done(failedReply(QStringLiteral("another request is still running")));
        return;
    }
    if (!url.isValid() || !ebay::isAllowedMainFrameUrl(url, m_options.allowLocalTestPages)) {
        emit done(failedReply(QStringLiteral("the URL is not an https URL on ebay.com")));
        return;
    }
    m_op = Op::Fetch;
    m_kind = kind;
    scheduleLoad(url);
}

void BrowserSession::signIn()
{
    if (isBusy()) {
        emit done(failedReply(QStringLiteral("another request is still running")));
        return;
    }
    m_op = Op::SignIn;
    m_signInShown = false;
    scheduleLoad(ebay::signInStartUrl());
}

void BrowserSession::cancel()
{
    if (!isBusy())
        return;
    stopTimers();
    stopLoading();
    hideWindow();
    finish(failedReply(QStringLiteral("stopped")));
}

void BrowserSession::scheduleLoad(const QUrl &url)
{
    m_url = url;
    qint64 wait = 0;
    if (m_sinceLastLoad.isValid())
        wait = std::max<qint64>(0, kPoliteDelayMs - m_sinceLastLoad.elapsed());
    m_delayTimer->start(static_cast<int>(wait));
}

void BrowserSession::startLoad()
{
    m_load = Load::Requested;
    m_loadClock.start();
    m_loadTimer->start(kLoadTimeoutMs);
    if (m_op == Op::Fetch)
        qCInfo(lcSession).noquote() << "fetch" << ebay::pageKindName(m_kind) << m_url.toString(QUrl::FullyEncoded);
    else
        qCInfo(lcSession).noquote() << "sign-in: opening" << ebay::urlForLog(m_url);
    m_page->load(m_url);
}

void BrowserSession::onLoadingChanged(const QWebEngineLoadingInfo &info)
{
    if (m_stopping) {
        m_sinceLastLoad.start();
        return;
    }
    if (info.status() == QWebEngineLoadingInfo::LoadStartedStatus) {
        if (m_load == Load::Requested)
            m_load = Load::Running;
        return;
    }
    // The load ended (succeeded, failed or stopped).
    const bool ok = info.status() == QWebEngineLoadingInfo::LoadSucceededStatus && !info.isErrorPage();
    if (m_load == Load::Requested)
        return; // the end of an earlier navigation, reported after load() was called
    m_sinceLastLoad.start();
    if (m_load == Load::Idle) {
        // A page the user opened in the visible window.
        if (m_op == Op::SignIn)
            checkSignedIn(ok);
        return;
    }
    m_load = Load::Idle;
    m_loadTimer->stop();
    if (m_op == Op::Fetch) {
        m_settleClock.start();
        runExtractor(ok);
    } else if (m_op == Op::SignIn) {
        checkSignedIn(ok);
    }
}

void BrowserSession::onLoadTimeout()
{
    if (m_load == Load::Idle)
        return;
    stopLoading();
    qCWarning(lcSession) << "page load timed out after" << kLoadTimeoutMs << "ms";
    finish(failedReply(QStringLiteral("the page did not finish loading within %1 s").arg(kLoadTimeoutMs / 1000)));
}

void BrowserSession::runExtractor(bool loadOk)
{
    if (m_op != Op::Fetch)
        return;
    m_lastLoadOk = loadOk;
    const quint64 serial = ++m_serial;
    m_scriptTimer->start(kScriptTimeoutMs);
    Extractor::run(m_page, m_kind, [this, loadOk, serial](const QJsonObject &result) {
        if (serial != m_serial || m_op != Op::Fetch)
            return;
        m_scriptTimer->stop();
        onExtracted(result, loadOk);
    });
}

void BrowserSession::onExtracted(const QJsonObject &result, bool loadOk)
{
    const QString page = result.value(QStringLiteral("page")).toString();
    // Content that is built after loading: look again until the key elements
    // appear or kSettleMs has passed.
    if (page == QLatin1String("unexpected") && loadOk && m_settleClock.elapsed() < kSettleMs) {
        m_settleTimer->start(kSettleStepMs);
        return;
    }
    const qint64 ms = m_loadClock.elapsed();
    if (page == QLatin1String("expected") && loadOk) {
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        qCInfo(lcSession).noquote() << "fetch" << ebay::pageKindName(m_kind) << "read in" << ms << "ms:"
                                    << logSummary(data);
        QJsonObject reply = statusReply(protocol::kStatusOk);
        reply.insert(QStringLiteral("result"), data);
        finish(reply);
        return;
    }
    if (page == QLatin1String("invalid")) {
        const QString reason = result.value(QStringLiteral("reason")).toString();
        qCWarning(lcSession).noquote() << "fetch" << ebay::pageKindName(m_kind) << "extractor failed:" << reason;
        finish(failedReply(reason));
        return;
    }
    QString shown = page;
    QString reason = result.value(QStringLiteral("reason")).toString();
    if (!loadOk && (page == QLatin1String("expected") || page == QLatin1String("unexpected"))) {
        // A failed or stopped load never counts as the expected page, even
        // when some of its elements are there.
        shown = QStringLiteral("unexpected");
        reason = QStringLiteral("the page did not load completely");
    }
    qCInfo(lcSession).noquote() << "fetch" << ebay::pageKindName(m_kind) << "unexpected page:" << shown << "-"
                                << reason << "- at" << ebay::urlForLog(m_page->url());
    QJsonObject reply = statusReply(protocol::kStatusUnexpectedPage);
    reply.insert(QStringLiteral("page"), shown);
    reply.insert(QStringLiteral("reason"), reason);
    showUnexpected(reply, messageFor(shown, loadOk));
}

void BrowserSession::checkSignedIn(bool loadOk)
{
    const QUrl url = m_page->url();
    if (!loadOk || ebay::classifyUrl(url) != ebay::UrlClass::AccountPage) {
        if (!m_signInShown) {
            m_signInShown = true;
            qCInfo(lcSession).noquote() << "sign-in: eBay shows" << ebay::urlForLog(url)
                                        << (loadOk ? "- window shown" : "(load failed) - window shown");
            showWindow(loadOk ? kSignInMessage
                              : QStringLiteral("The eBay page could not be loaded. Close this window to cancel."));
        }
        return;
    }
    const quint64 serial = ++m_serial;
    m_scriptTimer->start(kScriptTimeoutMs);
    m_page->runJavaScript(kSignedInCheck, QWebEngineScript::ApplicationWorld, [this, serial](const QVariant &value) {
        if (serial != m_serial || m_op != Op::SignIn)
            return;
        m_scriptTimer->stop();
        const QJsonObject check = QJsonDocument::fromJson(value.toString().toUtf8()).object();
        if (check.isEmpty() || check.value(QStringLiteral("errorPage")).toBool()
            || check.value(QStringLiteral("passwordField")).toBool()) {
            if (!m_signInShown) {
                m_signInShown = true;
                showWindow(kSignInMessage);
            }
            return;
        }
        qCInfo(lcSession) << "sign-in: signed in";
        hideWindow();
        QJsonObject reply = statusReply(protocol::kStatusOk);
        reply.insert(QStringLiteral("signedIn"), true);
        finish(reply);
    });
}

void BrowserSession::onScriptTimeout()
{
    if (!isBusy())
        return;
    qCWarning(lcSession) << "the page did not answer a script within" << kScriptTimeoutMs << "ms";
    stopTimers();
    stopLoading();
    hideWindow();
    finish(failedReply(QStringLiteral("the page did not answer within %1 s").arg(kScriptTimeoutMs / 1000)));
}

void BrowserSession::onWindowClosed()
{
    m_page->setInteractive(false);
    // Whatever the user had opened stops; the delay counts from here.
    stopLoading();
    m_sinceLastLoad.start();
    if (m_op == Op::Fetch && m_waitingForClose) {
        qCInfo(lcSession) << "window closed by the user";
        finish(m_replyAfterClose);
    } else if (m_op == Op::SignIn) {
        qCInfo(lcSession) << "sign-in: window closed by the user before signing in";
        stopTimers();
        QJsonObject reply = statusReply(protocol::kStatusWindowClosed);
        reply.insert(QStringLiteral("signedIn"), false);
        finish(reply);
    }
}

void BrowserSession::onNavigationBlocked(const QUrl &url)
{
    qCInfo(lcSession).noquote() << "navigation refused (outside ebay.com):" << url.scheme() << url.host();
    if (m_window->isVisible())
        m_window->setMessage(QStringLiteral("The app's browser only opens pages on ebay.com; it did not open %1.")
                                 .arg(url.host().isEmpty() ? url.scheme() : url.host()));
}

void BrowserSession::onRendererTerminated(int status, int exitCode)
{
    qCWarning(lcSession) << "page renderer ended: status" << status << "exit code" << exitCode;
    m_load = Load::Idle;
    m_sinceLastLoad.start();
    if (!isBusy())
        return;
    stopTimers();
    hideWindow();
    finish(failedReply(QStringLiteral("the page renderer ended (status %1, exit code %2)").arg(status).arg(exitCode)));
}

void BrowserSession::stopLoading()
{
    m_load = Load::Idle;
    m_loadTimer->stop();
    m_stopping = true;
    m_page->triggerAction(QWebEnginePage::Stop);
    m_stopping = false;
    m_sinceLastLoad.start();
}

void BrowserSession::stopTimers()
{
    m_delayTimer->stop();
    m_loadTimer->stop();
    m_settleTimer->stop();
    m_scriptTimer->stop();
}

void BrowserSession::showWindow(const QString &message)
{
    m_page->setInteractive(true);
    m_window->showWithMessage(message);
}

void BrowserSession::hideWindow()
{
    m_page->setInteractive(false);
    m_window->hide();
}

void BrowserSession::showUnexpected(const QJsonObject &reply, const QString &message)
{
    m_replyAfterClose = reply;
    m_waitingForClose = true;
    showWindow(message);
}

void BrowserSession::finish(const QJsonObject &reply)
{
    m_op = Op::None;
    m_waitingForClose = false;
    m_replyAfterClose = {};
    m_settleTimer->stop();
    m_scriptTimer->stop();
    ++m_serial;
    emit done(reply);
}

} // namespace app::browser
