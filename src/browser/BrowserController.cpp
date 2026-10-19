#include "browser/BrowserController.h"

#include "browser/CdpConnection.h"
#include "browser/Extractor.h"
#include "core/BrowserProfiles.h"
#include "ebay/PageCheck.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <utility>

namespace app::browser {

Q_STATIC_LOGGING_CATEGORY(lcBrowser, "browser.session")

namespace {

const QUrl kHomepage(QStringLiteral("https://www.ebay.com/"));

const QString kSignInMessage = QStringLiteral(
    "Sign in to eBay in the browser window: click \"Sign in\" at the top of the eBay page and sign in on eBay's "
    "own page. The app never sees or stores your password. Then press Continue.");

// Run on the My eBay page after sign-in: is it eBay's error page, and does it
// ask for a password (a sign-in or check form)?
const QString kSignedInCheck = QStringLiteral(
    "JSON.stringify({errorPage: document.title === 'Error Page | eBay'"
    " || document.querySelector('.error-header #error-info') !== null,"
    " passwordField: document.querySelector('input[type=password]') !== null})");

QString messageFor(const QString &page, bool loadOk)
{
    if (page == QLatin1String("error_page"))
        return QStringLiteral("eBay showed an error page instead of the expected page, so nothing was read. "
                              "Press Continue.");
    if (page == QLatin1String("sign_in"))
        return QStringLiteral("eBay asks you to sign in. You can sign in in the browser window; the app never "
                              "sees your password. Press Continue when you are done; the app does not reload "
                              "the page by itself.");
    if (page == QLatin1String("no_access"))
        return QStringLiteral("eBay opened the Seller Hub start page instead of Product Research: this account "
                              "has no Product Research access. Prices from sold listings will be marked "
                              "unconfirmed. Press Continue.");
    if (!loadOk)
        return QStringLiteral("The page could not be loaded completely, so nothing was read. Press Continue.");
    return QStringLiteral("This is not the page the app expected (it may be a security check, or eBay changed "
                          "the page), so nothing was read. The app does not solve or skip checks; you can "
                          "complete it in the browser window yourself. Press Continue when you are done; the app "
                          "does not reload the page by itself.");
}

QJsonObject statusReply(const char *status)
{
    return QJsonObject{{QStringLiteral("status"), QString::fromLatin1(status)}};
}

QJsonObject failedReply(const QString &error)
{
    QJsonObject reply = statusReply("failed");
    reply.insert(QStringLiteral("error"), error);
    return reply;
}

// Counts of what an extractor read, for the log: record count, the names of
// missing fields with how often each was missing, and for search pages the
// number of unrecognized card rows. No page text.
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
    QString summary = QStringLiteral("records=%1 missing=[%2]").arg(records.size()).arg(parts.join(QStringLiteral(", ")));
    if (data.value(QStringLiteral("unrecognizedRows")).isDouble())
        summary += QStringLiteral(" unrecognizedRows=%1").arg(data.value(QStringLiteral("unrecognizedRows")).toInteger());
    return summary;
}

// True when `current` is the page the app asked for: same scheme, host and
// path, and every query item of `requested` present in `current` with the
// same value (eBay may add items of its own, for example limit and offset).
bool isRequestedPage(const QUrl &current, const QUrl &requested)
{
    if (!current.isValid() || current.scheme() != requested.scheme()
        || current.host().compare(requested.host(), Qt::CaseInsensitive) != 0 || current.port() != requested.port()
        || current.path() != requested.path())
        return false;
    const QUrlQuery have(current);
    const QList<std::pair<QString, QString>> wanted = QUrlQuery(requested).queryItems(QUrl::FullyDecoded);
    return std::all_of(wanted.cbegin(), wanted.cend(), [&have](const std::pair<QString, QString> &item) {
        return have.allQueryItemValues(item.first, QUrl::FullyDecoded).contains(item.second);
    });
}

} // namespace

BrowserController::BrowserController(WorkFolder folder, InstalledBrowser browser, BrowserControllerOptions options,
                                     QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_browser(std::move(browser)), m_options(options),
      m_lock(BrowserProfiles::lockPath(m_folder, m_browser.id)), m_budget(m_folder.pageCountPath(), m_options.dailyPageCap)
{
    // No age limit (a session can be long); a lock left by a crashed app is
    // still taken over, because its process no longer runs.
    m_lock.setStaleLockTime(0);
    auto makeTimer = [this](auto slot) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, slot);
        return timer;
    };
    m_startTimer = makeTimer([this]() { fail(QStringLiteral("the browser did not start in time")); });
    m_delayTimer = makeTimer([this]() { startLoad(); });
    m_loadTimer = makeTimer([this]() {
        if (!m_loading)
            return;
        qCWarning(lcBrowser) << "page load timed out after" << kLoadTimeoutMs << "ms";
        command(QStringLiteral("Page.stopLoading"), {}, nullptr);
        m_loading = false;
        m_sinceLastLoad.start();
        const QString reason = QStringLiteral("the page did not finish loading within %1 s").arg(kLoadTimeoutMs / 1000);
        if (m_op == Op::Fetch) {
            // Shown to the user like any page that is not the expected one.
            QJsonObject reply = statusReply("unexpected_page");
            reply.insert(QStringLiteral("page"), QStringLiteral("unexpected"));
            reply.insert(QStringLiteral("reason"), reason);
            waitForUser(reply, messageFor(QStringLiteral("unexpected"), false));
        } else {
            finish(failedReply(reason));
        }
    });
    m_settleTimer = makeTimer([this]() { runExtractor(); });
    m_scriptTimer = makeTimer([this]() {
        qCWarning(lcBrowser) << "the page did not answer a script within" << kScriptTimeoutMs << "ms";
        finish(failedReply(QStringLiteral("the page did not answer within %1 s").arg(kScriptTimeoutMs / 1000)));
    });
}

BrowserController::~BrowserController()
{
    if (!m_cdp)
        return;
    // Nobody may receive signals from an object being destroyed.
    blockSignals(true);
    m_op = Op::None;
    ++m_serial;
    m_stopping = true;
    // Its signals would reach this half-destroyed object.
    m_cdp->disconnect(this);
    if (m_cdp->isRunning()) {
        if (m_cdp->isConnected()) {
            m_cdp->send(QStringLiteral("Browser.close"));
            m_cdp->flush();
            m_cdp->waitForExit(kCloseTimeoutMs);
        } else {
            m_cdp->waitForExit(0);
        }
    }
    if (m_lock.isLocked())
        m_lock.unlock();
}

bool BrowserController::start(QString *error)
{
    auto failStart = [this, error](const QString &message) {
        if (m_lock.isLocked())
            m_lock.unlock();
        if (error)
            *error = message;
        return false;
    };
    if (m_cdp)
        return failStart(QStringLiteral("the browser is already running"));
    if (!BrowserProfiles::isValidId(m_browser.id))
        return failStart(QStringLiteral("not a browser id"));
    const QString profile = BrowserProfiles::profileDir(m_folder, m_browser.id);
    if (!QDir().mkpath(profile))
        return failStart(QStringLiteral("cannot create the browser profile folder"));
    if (!m_lock.tryLock(0)) {
        return failStart(m_lock.error() == QLockFile::LockFailedError
                             ? QStringLiteral("this browser profile is already in use by the app")
                             : QStringLiteral("cannot create the browser profile lock"));
    }

    m_cdp = new CdpConnection(this);
    m_cdp->setErrorFile(m_options.browserErrorFile);
    connect(m_cdp, &CdpConnection::connected, this, [this]() {
        if (!m_stopping && !m_failed)
            attachToFirstPage(100);
    });
    connect(m_cdp, &CdpConnection::response, this, &BrowserController::onResponse);
    connect(m_cdp, &CdpConnection::cdpEvent, this, &BrowserController::onEvent);
    connect(m_cdp, &CdpConnection::protocolError, this, [this](const QString &message) {
        if (!m_stopping)
            fail(message);
    });
    connect(m_cdp, &CdpConnection::finished, this, &BrowserController::onBrowserFinished);

    // A separate instance with its own profile folder; no automation switch.
    // --disk-cache-dir keeps the HTTP cache in the profile folder: on macOS
    // Chromium otherwise moves the cache of a profile under ~/Library/
    // Application Support to ~/Library/Caches (chrome_paths_mac.mm,
    // GetUserCacheDirectory). --no-first-run and --no-default-browser-check
    // keep a fresh profile from opening welcome pages and asking to become
    // the default browser.
    const QStringList arguments{QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(profile),
                                QStringLiteral("--disk-cache-dir=") + QDir::toNativeSeparators(profile),
                                QStringLiteral("--no-first-run"), QStringLiteral("--no-default-browser-check"),
                                QStringLiteral("about:blank")};
    QString startError;
    if (!m_cdp->start(m_browser.executable, arguments, &startError)) {
        delete m_cdp;
        m_cdp = nullptr;
        return failStart(startError);
    }
    m_sinceLastLoad.start();
    m_startTimer->start(kStartTimeoutMs);
    qCInfo(lcBrowser).noquote() << "browser started:" << m_browser.name << "pid" << m_cdp->processId();
    return true;
}

bool BrowserController::isRunning() const
{
    return m_cdp && m_cdp->isRunning();
}

qint64 BrowserController::processId() const
{
    return m_cdp ? m_cdp->processId() : 0;
}

void BrowserController::command(const QString &method, const QJsonObject &params, Callback callback, bool onPage)
{
    if (!m_cdp)
        return;
    const int id = m_cdp->send(method, params, onPage ? m_sessionId : QString());
    if (id > 0 && callback)
        m_callbacks.emplace(id, std::move(callback));
}

void BrowserController::onResponse(int id, const QJsonObject &result, const QJsonObject &error)
{
    const auto it = m_callbacks.find(id);
    if (it == m_callbacks.end())
        return;
    Callback callback = std::move(it->second);
    m_callbacks.erase(it);
    callback(result, error);
}

void BrowserController::attachToFirstPage(int attemptsLeft)
{
    // The browser opens its first tab (about:blank) shortly after it starts.
    command(QStringLiteral("Target.getTargets"), {}, [this, attemptsLeft](const QJsonObject &result, const QJsonObject &) {
        if (m_stopping || m_failed)
            return;
        // The browser restores the tabs of its previous session in this
        // profile and adds the command-line about:blank (recon 2026-10-19,
        // phase 4: 4 tabs at start). The app keeps exactly one tab: the first
        // about:blank, else the first tab; the others - pages the app's own
        // profile kept from earlier sessions - are closed.
        QStringList pages;
        QString keep;
        for (const QJsonValue &value : result.value(QStringLiteral("targetInfos")).toArray()) {
            const QJsonObject target = value.toObject();
            if (target.value(QStringLiteral("type")).toString() != QLatin1String("page"))
                continue;
            const QString id = target.value(QStringLiteral("targetId")).toString();
            pages.append(id);
            m_startTabs.insert(id);
            if (keep.isEmpty() && target.value(QStringLiteral("url")).toString() == QLatin1String("about:blank"))
                keep = id;
        }
        if (!pages.isEmpty()) {
            if (keep.isEmpty())
                keep = pages.first();
            for (const QString &id : std::as_const(pages)) {
                if (id != keep)
                    command(QStringLiteral("Target.closeTarget"), {{QStringLiteral("targetId"), id}}, nullptr, false);
            }
            qCInfo(lcBrowser) << "browser tabs at start:" << pages.size() << "- kept 1, closed" << pages.size() - 1;
            m_targetId = keep;
            const QJsonObject params{{QStringLiteral("targetId"), m_targetId}, {QStringLiteral("flatten"), true}};
            command(QStringLiteral("Target.attachToTarget"), params, [this](const QJsonObject &attached, const QJsonObject &error) {
                m_sessionId = attached.value(QStringLiteral("sessionId")).toString();
                if (m_sessionId.isEmpty()) {
                    fail(QStringLiteral("cannot attach to the browser tab: %1").arg(error.value(QStringLiteral("message")).toString()));
                    return;
                }
                command(QStringLiteral("Page.enable"), {}, nullptr);
                command(QStringLiteral("Network.enable"), {}, nullptr);
                command(QStringLiteral("Target.activateTarget"), {{QStringLiteral("targetId"), m_targetId}}, nullptr, false);
                // Tabs that pages open later are reported (Target.targetCreated).
                command(QStringLiteral("Target.setDiscoverTargets"), {{QStringLiteral("discover"), true}}, nullptr, false);
                command(QStringLiteral("Page.getFrameTree"), {}, [this](const QJsonObject &tree, const QJsonObject &) {
                    m_mainFrameId = tree.value(QStringLiteral("frameTree")).toObject().value(QStringLiteral("frame")).toObject().value(QStringLiteral("id")).toString();
                    command(QStringLiteral("Browser.getWindowForTarget"), {{QStringLiteral("targetId"), m_targetId}},
                            [this](const QJsonObject &window, const QJsonObject &) {
                                if (m_stopping || m_failed)
                                    return;
                                m_windowId = window.value(QStringLiteral("windowId")).toInt(-1);
                                // The window stays out of the way until the user has to act.
                                setWindowState("minimized");
                                m_startTimer->stop();
                                m_ready = true;
                                qCInfo(lcBrowser) << "browser ready";
                                emit ready();
                            }, false);
                });
            }, false);
        }
        if (!m_targetId.isEmpty())
            return;
        if (attemptsLeft <= 0) {
            fail(QStringLiteral("the browser opened no tab"));
            return;
        }
        QTimer::singleShot(200, this, [this, attemptsLeft]() { attachToFirstPage(attemptsLeft - 1); });
    }, false);
}

void BrowserController::setWindowState(const char *state)
{
    if (m_windowId < 0)
        return;
    const QJsonObject params{{QStringLiteral("windowId"), m_windowId},
                             {QStringLiteral("bounds"), QJsonObject{{QStringLiteral("windowState"), QString::fromLatin1(state)}}}};
    command(QStringLiteral("Browser.setWindowBounds"), params, nullptr, false);
    if (qstrcmp(state, "normal") == 0)
        command(QStringLiteral("Page.bringToFront"), {}, nullptr);
}

void BrowserController::onEvent(const QString &method, const QJsonObject &params, const QString &sessionId)
{
    if (method == QLatin1String("Target.detachedFromTarget")
        && params.value(QStringLiteral("sessionId")).toString() == m_sessionId && !m_sessionId.isEmpty()) {
        if (!m_stopping)
            fail(QStringLiteral("the app's browser tab was closed"));
        return;
    }
    if (method == QLatin1String("Target.targetCreated")) {
        // A new tab or window (discovery also reports the tabs that existed
        // at start once; those are not new). The app ignores it and keeps its
        // own tab in front - except while the user acts in the window (a
        // sign-in may need the new window).
        const QJsonObject info = params.value(QStringLiteral("targetInfo")).toObject();
        const QString id = info.value(QStringLiteral("targetId")).toString();
        if (info.value(QStringLiteral("type")).toString() == QLatin1String("page") && id != m_targetId
            && !m_startTabs.contains(id)) {
            m_startTabs.insert(id);
            const bool userActs = m_waitingForUser || m_op == Op::SignInWaiting;
            qCInfo(lcBrowser) << "a page opened another tab;" << (userActs ? "left to the user" : "the app's tab stays in front");
            if (!userActs)
                command(QStringLiteral("Target.activateTarget"), {{QStringLiteral("targetId"), m_targetId}}, nullptr, false);
        }
        return;
    }
    if (sessionId.isEmpty() || sessionId != m_sessionId)
        return;
    if (method == QLatin1String("Page.frameNavigated")) {
        const QJsonObject frame = params.value(QStringLiteral("frame")).toObject();
        if (!frame.contains(QStringLiteral("parentId")))
            m_mainFrameId = frame.value(QStringLiteral("id")).toString();
    } else if (method == QLatin1String("Network.responseReceived")) {
        // A main document's response: its loader, status and final URL (after
        // redirects). Kept until Page.navigate has said which loader is ours.
        if (params.value(QStringLiteral("type")).toString() == QLatin1String("Document")
            && params.value(QStringLiteral("frameId")).toString() == m_mainFrameId) {
            const QJsonObject response = params.value(QStringLiteral("response")).toObject();
            if (m_loading) {
                m_documents.insert(params.value(QStringLiteral("loaderId")).toString(),
                                   {response.value(QStringLiteral("status")).toInt(), QUrl(response.value(QStringLiteral("url")).toString())});
            } else if (m_waitingForUser) {
                // A page the user opened while the app waits: its status
                // decides whether it may be read after Continue.
                m_userDocumentStatus = response.value(QStringLiteral("status")).toInt();
                m_userDocumentLoaded = false;
            }
        }
    } else if (method == QLatin1String("Page.loadEventFired")) {
        // A page load the app did not make (the user's, in the shown window)
        // also counts for the pace: the next load of the app waits from its
        // end.
        if (!m_loading) {
            m_sinceLastLoad.start();
            if (m_waitingForUser && m_userDocumentStatus != 0)
                m_userDocumentLoaded = true;
        }
        // Only the load of the app's own navigation ends a fetch: its loader's
        // document response must have arrived. A load event of a page the user
        // opened, or of the page before, does not.
        if (m_loading && !m_loaderId.isEmpty() && m_documents.contains(m_loaderId)) {
            const Document document = m_documents.value(m_loaderId);
            m_httpStatus = document.status;
            m_documentUrl = document.url;
            endLoad(m_navigationError.isEmpty() && m_httpStatus >= 200 && m_httpStatus < 400);
        }
    }
}

void BrowserController::fetch(const QUrl &url, ebay::PageKind kind)
{
    if (!m_ready || isBusy()) {
        emit done(failedReply(m_ready ? QStringLiteral("another request is still running")
                                      : QStringLiteral("the browser is not ready")));
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

void BrowserController::signIn()
{
    if (!m_ready || isBusy()) {
        emit done(failedReply(m_ready ? QStringLiteral("another request is still running")
                                      : QStringLiteral("the browser is not ready")));
        return;
    }
    m_op = Op::SignInOpening;
    scheduleLoad(kHomepage);
}

void BrowserController::continueAfterUser()
{
    if (m_op == Op::Fetch && m_waitingForUser) {
        qCInfo(lcBrowser) << "the user pressed Continue; reading the page as it is now";
        setWindowState("minimized");
        m_waitingForUser = false;
        rereadAfterUser();
    } else if (m_op == Op::SignInWaiting) {
        qCInfo(lcBrowser) << "sign-in: the user pressed Continue; checking the My eBay page";
        setWindowState("minimized");
        m_op = Op::SignInChecking;
        scheduleLoad(ebay::signInStartUrl());
    }
}

void BrowserController::cancel()
{
    if (!isBusy())
        return;
    stopTimers();
    if (m_loading)
        command(QStringLiteral("Page.stopLoading"), {}, nullptr);
    m_loading = false;
    m_sinceLastLoad.start();
    setWindowState("minimized");
    finish(failedReply(QStringLiteral("stopped")));
}

void BrowserController::stop()
{
    if (!m_cdp || m_stopping)
        return;
    m_stopping = true;
    if (isBusy())
        cancel();
    if (!m_cdp->isRunning())
        return;
    if (!m_cdp->isConnected()) {
        // Nothing to ask yet: the browser has no session of the app's.
        m_cdp->waitForExit(0);
        return;
    }
    qCInfo(lcBrowser) << "closing the browser";
    command(QStringLiteral("Browser.close"), {}, nullptr, false);
    // Killed if it does not exit in time.
    QTimer::singleShot(kCloseTimeoutMs, this, [this]() {
        if (m_cdp && m_cdp->isRunning())
            m_cdp->waitForExit(0);
    });
}

void BrowserController::onBrowserFinished(int exitCode, bool crashed)
{
    if (!m_stopping && !m_failed)
        fail(QStringLiteral("the browser was closed"));
    stopTimers();
    m_ready = false;
    m_callbacks.clear();
    if (m_lock.isLocked())
        m_lock.unlock();
    const bool normal = m_stopping && !crashed && !m_failed;
    qCInfo(lcBrowser) << "browser exited, code" << exitCode << (normal ? "(normal)" : "(abnormal)");
    emit finished(normal);
}

void BrowserController::scheduleLoad(const QUrl &url)
{
    m_url = url;
    const qint64 minMs = std::max(0, m_options.pageDelayMinMs);
    const qint64 maxMs = std::max<qint64>(minMs, m_options.pageDelayMaxMs);
    const qint64 delay = QRandomGenerator::global()->bounded(minMs, maxMs + 1);
    const qint64 wait = std::clamp<qint64>(delay - m_sinceLastLoad.elapsed(), 0, std::numeric_limits<int>::max());
    m_delayTimer->start(static_cast<int>(wait));
}

void BrowserController::startLoad()
{
    QString budgetError;
    const PageBudget::Take take = m_budget.take(&budgetError);
    if (take != PageBudget::Take::Taken) {
        qCWarning(lcBrowser).noquote() << "page not loaded:" << budgetError;
        QJsonObject reply = take == PageBudget::Take::CapReached ? statusReply("limit_reached") : statusReply("failed");
        reply.insert(QStringLiteral("error"), budgetError);
        finish(reply);
        return;
    }
    m_loading = true;
    ++m_serial;
    m_navigationError.clear();
    m_loaderId.clear();
    m_documents.clear();
    m_httpStatus = 0;
    m_documentUrl = m_url;
    m_loadClock.start();
    m_loadTimer->start(kLoadTimeoutMs);
    if (m_op == Op::Fetch)
        qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind) << m_url.toString(QUrl::FullyEncoded);
    else
        qCInfo(lcBrowser).noquote() << "sign-in: opening" << ebay::urlForLog(m_url);
    const quint64 serial = m_serial;
    command(QStringLiteral("Page.navigate"), {{QStringLiteral("url"), m_url.toString(QUrl::FullyEncoded)}},
            [this, serial](const QJsonObject &result, const QJsonObject &error) {
                if (serial != m_serial || !m_loading)
                    return;
                // errorText: "present if and only if navigation has failed".
                const QString text = result.value(QStringLiteral("errorText")).toString();
                if (!text.isEmpty() || !error.isEmpty()) {
                    m_navigationError = text.isEmpty() ? error.value(QStringLiteral("message")).toString() : text;
                    endLoad(false);
                    return;
                }
                m_loaderId = result.value(QStringLiteral("loaderId")).toString();
            });
}

void BrowserController::endLoad(bool ok)
{
    m_loading = false;
    m_loadTimer->stop();
    m_sinceLastLoad.start();
    m_lastLoadOk = ok;
    if (m_op == Op::Fetch) {
        m_settleClock.start();
        runExtractor();
    } else if (m_op == Op::SignInOpening) {
        m_op = Op::SignInWaiting;
        qCInfo(lcBrowser) << "sign-in: window shown";
        setWindowState("normal");
        emit needsUser(ok ? kSignInMessage
                          : QStringLiteral("The eBay homepage could not be loaded. You can still try in the browser "
                                           "window, then press Continue."));
    } else if (m_op == Op::SignInChecking) {
        checkSignedIn(ok);
    }
}

void BrowserController::runExtractor()
{
    if (m_op != Op::Fetch)
        return;
    if (const std::optional<QJsonObject> outcome = Extractor::outcomeFromUrl(m_documentUrl, m_kind)) {
        onExtracted(*outcome);
        return;
    }
    evaluateExtractor(m_documentUrl, [this](const QJsonObject &result) { onExtracted(result); });
}

void BrowserController::evaluateExtractor(const QUrl &pageUrl, std::function<void(const QJsonObject &)> handle)
{
    const quint64 serial = m_serial;
    m_scriptTimer->start(kScriptTimeoutMs);
    const QJsonObject changed{{QStringLiteral("page"), QStringLiteral("unexpected")},
                              {QStringLiteral("reason"), QStringLiteral("the page changed while it was read")}};
    // An isolated world of the app's own: page scripts cannot interfere
    // with the extractor's functions; the DOM is shared.
    const QJsonObject world{{QStringLiteral("frameId"), m_mainFrameId}, {QStringLiteral("worldName"), QStringLiteral("extractor")}};
    command(QStringLiteral("Page.createIsolatedWorld"), world,
            [this, serial, pageUrl, handle, changed](const QJsonObject &created, const QJsonObject &error) {
        if (serial != m_serial || m_op != Op::Fetch)
            return;
        // Without the isolated world's id the script would run in the page's
        // own world: never.
        if (!error.isEmpty() || !created.value(QStringLiteral("executionContextId")).isDouble()) {
            // The page changed under the extractor (for example a script
            // redirect): look again or show it, as for any unexpected page.
            m_scriptTimer->stop();
            handle(changed);
            return;
        }
        const QJsonObject params{{QStringLiteral("expression"), Extractor::script(m_kind, pageUrl)},
                                 {QStringLiteral("contextId"), created.value(QStringLiteral("executionContextId"))},
                                 {QStringLiteral("returnByValue"), true}};
        command(QStringLiteral("Runtime.evaluate"), params,
                [this, serial, handle, changed](const QJsonObject &evaluated, const QJsonObject &error) {
            if (serial != m_serial || m_op != Op::Fetch)
                return;
            m_scriptTimer->stop();
            if (!error.isEmpty()) {
                // The isolated world went away with a navigation.
                handle(changed);
                return;
            }
            const QJsonValue value = evaluated.contains(QStringLiteral("exceptionDetails"))
                                         ? QJsonValue()
                                         : evaluated.value(QStringLiteral("result")).toObject().value(QStringLiteral("value"));
            handle(Extractor::parseResult(value));
        });
    });
}

void BrowserController::rereadAfterUser()
{
    // The page the tab shows now: the current entry of its history.
    const quint64 serial = m_serial;
    m_scriptTimer->start(kScriptTimeoutMs);
    command(QStringLiteral("Page.getNavigationHistory"), {}, [this, serial](const QJsonObject &history, const QJsonObject &error) {
        if (serial != m_serial || m_op != Op::Fetch)
            return;
        m_scriptTimer->stop();
        const QJsonArray entries = history.value(QStringLiteral("entries")).toArray();
        const int index = history.value(QStringLiteral("currentIndex")).toInt(-1);
        const QUrl current = error.isEmpty() && index >= 0 && index < entries.size()
                                 ? QUrl(entries.at(index).toObject().value(QStringLiteral("url")).toString())
                                 : QUrl();
        // The document shown must have loaded successfully: the app's own
        // load when the user opened no page, else the user's last page
        // (completely loaded, HTTP status below 400). A failed load is never
        // read as the expected page.
        const bool documentOk = m_userDocumentStatus == 0
                                    ? m_lastLoadOk
                                    : m_userDocumentLoaded && m_userDocumentStatus >= 200 && m_userDocumentStatus < 400;
        if (!documentOk) {
            qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind)
                                        << "after Continue: the page shown did not load successfully";
            finish(m_replyAfterUser);
            return;
        }
        if (!isRequestedPage(current, m_url) || Extractor::outcomeFromUrl(current, m_kind)) {
            qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind)
                                        << "after Continue: still not the requested page, at" << ebay::urlForLog(current);
            finish(m_replyAfterUser);
            return;
        }
        evaluateExtractor(current, [this](const QJsonObject &result) {
            if (result.value(QStringLiteral("page")).toString() != QLatin1String("expected")) {
                qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind)
                                            << "after Continue: still not the expected page:"
                                            << result.value(QStringLiteral("reason")).toString();
                finish(m_replyAfterUser);
                return;
            }
            const QJsonObject data = result.value(QStringLiteral("data")).toObject();
            qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind) << "read after Continue:" << logSummary(data);
            QJsonObject reply = statusReply("ok");
            reply.insert(QStringLiteral("result"), data);
            finish(reply);
        });
    });
}

void BrowserController::onExtracted(const QJsonObject &result)
{
    const QString page = result.value(QStringLiteral("page")).toString();
    const bool loadOk = m_lastLoadOk;
    // Content that is built after loading: look again until the key elements
    // appear or kSettleMs has passed.
    if (page == QLatin1String("unexpected") && loadOk && m_settleClock.elapsed() < kSettleMs) {
        m_settleTimer->start(kSettleStepMs);
        return;
    }
    const qint64 ms = m_loadClock.elapsed();
    if (page == QLatin1String("expected") && loadOk) {
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind) << "read in" << ms << "ms:" << logSummary(data);
        QJsonObject reply = statusReply("ok");
        reply.insert(QStringLiteral("result"), data);
        finish(reply);
        return;
    }
    if (page == QLatin1String("invalid")) {
        const QString reason = result.value(QStringLiteral("reason")).toString();
        qCWarning(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind) << "extractor failed:" << reason;
        finish(failedReply(reason));
        return;
    }
    QString shown = page;
    QString reason = result.value(QStringLiteral("reason")).toString();
    if (!loadOk && (page == QLatin1String("expected") || page == QLatin1String("unexpected"))) {
        // A failed load never counts as the expected page.
        shown = QStringLiteral("unexpected");
        reason = m_httpStatus >= 400 ? QStringLiteral("the page answered with HTTP status %1").arg(m_httpStatus)
                                     : QStringLiteral("the page did not load completely");
    }
    qCInfo(lcBrowser).noquote() << "fetch" << ebay::pageKindName(m_kind) << "unexpected page:" << shown << "-" << reason
                                << "- at" << ebay::urlForLog(m_documentUrl);
    QJsonObject reply = statusReply("unexpected_page");
    reply.insert(QStringLiteral("page"), shown);
    reply.insert(QStringLiteral("reason"), reason);
    waitForUser(reply, messageFor(shown, loadOk));
}

void BrowserController::checkSignedIn(bool loadOk)
{
    auto answer = [this](bool signedIn) {
        qCInfo(lcBrowser) << "sign-in:" << (signedIn ? "signed in" : "not signed in");
        QJsonObject reply = statusReply("ok");
        reply.insert(QStringLiteral("signedIn"), signedIn);
        finish(reply);
    };
    if (!loadOk || ebay::classifyUrl(m_documentUrl) != ebay::UrlClass::AccountPage) {
        answer(false);
        return;
    }
    const quint64 serial = m_serial;
    m_scriptTimer->start(kScriptTimeoutMs);
    const QJsonObject world{{QStringLiteral("frameId"), m_mainFrameId}, {QStringLiteral("worldName"), QStringLiteral("extractor")}};
    command(QStringLiteral("Page.createIsolatedWorld"), world, [this, serial, answer](const QJsonObject &created, const QJsonObject &error) {
        if (serial != m_serial || m_op != Op::SignInChecking)
            return;
        // Never run the check in the page's own script world.
        if (!error.isEmpty() || !created.value(QStringLiteral("executionContextId")).isDouble()) {
            m_scriptTimer->stop();
            answer(false);
            return;
        }
        const QJsonObject params{{QStringLiteral("expression"), kSignedInCheck},
                                 {QStringLiteral("contextId"), created.value(QStringLiteral("executionContextId"))},
                                 {QStringLiteral("returnByValue"), true}};
        command(QStringLiteral("Runtime.evaluate"), params, [this, serial, answer](const QJsonObject &evaluated, const QJsonObject &) {
            if (serial != m_serial || m_op != Op::SignInChecking)
                return;
            m_scriptTimer->stop();
            const QJsonObject check = QJsonDocument::fromJson(
                evaluated.value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toString().toUtf8()).object();
            answer(!check.isEmpty() && !check.value(QStringLiteral("errorPage")).toBool()
                   && !check.value(QStringLiteral("passwordField")).toBool());
        });
    });
}

void BrowserController::waitForUser(const QJsonObject &reply, const QString &message)
{
    m_replyAfterUser = reply;
    m_waitingForUser = true;
    m_userDocumentStatus = 0;
    m_userDocumentLoaded = false;
    setWindowState("normal");
    emit needsUser(message);
}

void BrowserController::finish(const QJsonObject &reply)
{
    // A copy: `reply` may be m_replyAfterUser, which is cleared below.
    const QJsonObject answer = reply;
    m_op = Op::None;
    m_waitingForUser = false;
    m_replyAfterUser = {};
    m_settleTimer->stop();
    m_scriptTimer->stop();
    ++m_serial;
    emit done(answer);
}

void BrowserController::fail(const QString &error)
{
    if (m_failed)
        return;
    m_failed = true;
    qCWarning(lcBrowser).noquote() << "browser session failed:" << error;
    stopTimers();
    if (isBusy())
        finish(failedReply(error));
    emit failed(error);
    // Close what is left of the browser (a no-op when stop() already ran).
    stop();
}

void BrowserController::stopTimers()
{
    m_startTimer->stop();
    m_delayTimer->stop();
    m_loadTimer->stop();
    m_settleTimer->stop();
    m_scriptTimer->stop();
}

} // namespace app::browser
