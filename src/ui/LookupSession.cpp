#include "ui/LookupSession.h"

#include "browser/BrowserCatalog.h"
#include "browser/BrowserController.h"
#include "core/Settings.h"
#include "ebay/SearchUrls.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"

#include <QBuffer>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace app::ui {

namespace {

// The listing photo: at most this many bytes are downloaded, within this
// time, and it is kept at most this many pixels wide and high.
constexpr qint64 kMaxPhotoBytes = 8 * 1024 * 1024;
constexpr int kPhotoTimeoutMs = 20000;
constexpr int kPhotoSide = 480;
constexpr int kPhotoJpegQuality = 85;

enum Step { StepBrowser, StepListing, StepSold, StepForSale, StepResearch, StepSaving };

} // namespace

LookupSession::LookupSession(WorkFolder folder, lookup::KnownModels *known, const lookup::MatchWords *words,
                             const lookup::LookupStore *store, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_known(known), m_words(words), m_store(store)
{
}

LookupSession::~LookupSession() = default;

QStringList LookupSession::steps() const
{
    return {QStringLiteral("Starting the browser"), QStringLiteral("Reading the listing"),
            QStringLiteral("Sold listings"), QStringLiteral("Listings for sale now"),
            QStringLiteral("Product Research"), QStringLiteral("Saving")};
}

QString LookupSession::stateName() const
{
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Starting:
        return QStringLiteral("starting");
    case State::Reading:
        return QStringLiteral("reading");
    case State::AskIdentity:
        return QStringLiteral("askIdentity");
    case State::Searching:
        return QStringLiteral("searching");
    case State::WaitingForUser:
        return QStringLiteral("waitingForUser");
    case State::Closing:
        return QStringLiteral("closing");
    }
    return {};
}

void LookupSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void LookupSession::setError(const QString &error)
{
    m_errorText = error;
    if (!error.isEmpty())
        qWarning().noquote() << "lookup:" << error;
    emit errorChanged();
}

bool LookupSession::start(const QString &input)
{
    if (isRunning()) {
        setError(QStringLiteral("A lookup is already running."));
        return false;
    }
    if (ebay::itemIdFromInput(input).isEmpty()) {
        setError(QStringLiteral("This is not an eBay listing link (https://www.ebay.com/itm/...) or a 12-digit item number."));
        return false;
    }
    return startSession(input.trimmed(), std::nullopt);
}

bool LookupSession::startCorrected(const lookup::SavedLookup &saved, const QString &brand, const QString &model,
                                   const QString &productName)
{
    if (isRunning()) {
        setError(QStringLiteral("A lookup is already running."));
        return false;
    }
    if (saved.result.item.itemId.isEmpty()) {
        setError(QStringLiteral("This saved lookup has no listing to search for."));
        return false;
    }
    QString error;
    if (!saveIdentity(saved.result.item.itemId, {brand, model, productName}, saved.id, &error)) {
        setError(error);
        return false;
    }
    return startSession(saved.input, saved.result.item);
}

bool LookupSession::startSession(const QString &input, const std::optional<lookup::ItemRecord> &item)
{
    const Settings settings(m_folder.settingsPath());
    const std::optional<browser::InstalledBrowser> chosen =
        browser::BrowserCatalog::choose(browser::BrowserCatalog::installed(), settings.preferredBrowser());
    if (!chosen) {
        setError(QStringLiteral("No supported browser is installed. Install Brave, Google Chrome or Microsoft Edge "
                                "(on Linux also Chromium) from its official page."));
        return false;
    }
    browser::BrowserControllerOptions options;
    options.pageDelayMinMs = settings.pageDelayMinMs();
    options.pageDelayMaxMs = settings.pageDelayMaxMs();
    options.dailyPageCap = settings.dailyPageCap();
    m_depths = {settings.soldDepth(), settings.activeDepth(), settings.researchDepth()};
    m_historyDays = settings.soldHistoryDays();

    auto *controller = new browser::BrowserController(m_folder, *chosen, options, this);
    QString error;
    if (!controller->start(&error)) {
        delete controller;
        setError(QStringLiteral("%1 could not be started: %2").arg(chosen->name, error));
        return false;
    }
    m_controller = controller;
    m_input = input;
    m_savedItem = item;
    m_savedOnce = false;
    m_browserError.clear();
    m_browserName = chosen->name;
    m_step = StepBrowser;
    m_pages = 0;
    m_lastLine = QStringLiteral("Starting %1...").arg(chosen->name);
    m_userMessage.clear();
    setError(QString());
    connect(m_controller, &browser::BrowserController::ready, this, &LookupSession::onReady);
    connect(m_controller, &browser::BrowserController::done, this, [this](const QJsonObject &reply) {
        if (m_runner)
            m_runner->onReply(reply);
    });
    connect(m_controller, &browser::BrowserController::needsUser, this, [this](const QString &message) {
        qInfo().noquote() << "lookup: waiting for the user in the browser window:" << message;
        m_userMessage = message;
        if (m_state != State::WaitingForUser)
            m_stateBeforeUser = m_state;
        m_state = State::WaitingForUser;
        emit stateChanged();
    });
    connect(m_controller, &browser::BrowserController::failed, this, [this](const QString &message) {
        m_browserError = message;
        qWarning().noquote() << "lookup: browser session failed:" << message;
    });
    connect(m_controller, &browser::BrowserController::finished, this, &LookupSession::onBrowserFinished);
    qInfo().noquote() << "lookup: starting" << chosen->name << "for" << input;
    setState(State::Starting);
    emit progressChanged();
    return true;
}

void LookupSession::onReady()
{
    m_runner = new lookup::LookupRunner(
        [this](const QUrl &url, ebay::PageKind kind) {
            ++m_pages;
            switch (kind) {
            case ebay::PageKind::Item:
                m_step = StepListing;
                break;
            case ebay::PageKind::SoldSearch:
                m_step = StepSold;
                break;
            case ebay::PageKind::ActiveSearch:
                m_step = StepForSale;
                break;
            case ebay::PageKind::Research:
                m_step = StepResearch;
                break;
            }
            emit progressChanged();
            m_controller->fetch(url, kind);
        },
        m_known, m_words, this);
    connect(m_runner, &lookup::LookupRunner::progress, this, [this](const QString &line) {
        qInfo().noquote() << "lookup:" << line;
        m_lastLine = line;
        emit progressChanged();
    });
    connect(m_runner, &lookup::LookupRunner::itemReady, this, &LookupSession::onItemReady);
    connect(m_runner, &lookup::LookupRunner::finished, this, &LookupSession::onRunnerFinished);
    QString error;
    const bool started = m_savedItem ? m_runner->startFromItem(*m_savedItem, &error) : m_runner->startItem(m_input, &error);
    if (!started) {
        // Not a lookup yet: nothing to save.
        setError(error);
        m_savedOnce = true;
        setState(State::Closing);
        m_controller->stop();
        return;
    }
    m_step = StepListing;
    setState(State::Reading);
    emit progressChanged();
}

void LookupSession::onItemReady()
{
    const lookup::LookupResult &result = m_runner->result();
    if (!m_savedItem)
        fetchPhoto(result.item);
    if (result.identity.canSearch()) {
        startSearches();
        return;
    }
    // Rule 26: no confident model, no search; the user enters it.
    setState(State::AskIdentity);
}

void LookupSession::startSearches()
{
    QString error;
    if (!m_runner->startComps(m_runner->result().queries, m_depths, m_historyDays, &error)) {
        setError(error);
        setState(State::AskIdentity);
        return;
    }
    setState(State::Searching);
}

bool LookupSession::saveIdentity(const QString &itemId, const lookup::ConfirmedIdentity &identity,
                                 std::optional<qint64> lookupId, QString *error)
{
    if (identity.brand.simplified().isEmpty() || identity.model.simplified().isEmpty()) {
        *error = QStringLiteral("Enter the brand and the model: nothing is searched without them.");
        return false;
    }
    if (!m_known->saveConfirmed(m_folder.knownModelsPath(), itemId, identity, error)) {
        *error = QStringLiteral("The identity could not be saved: %1").arg(*error);
        return false;
    }
    QString dbError;
    if (!m_store->saveUserIdentity(itemId, identity, lookupId, &dbError))
        qWarning().noquote() << "lookup: the identity was saved to the known models but not recorded in the database:"
                             << dbError;
    return true;
}

bool LookupSession::submitIdentity(const QString &brand, const QString &model, const QString &productName)
{
    if (m_state != State::AskIdentity || !m_runner || !m_runner->isWaitingForQueries())
        return false;
    const lookup::ConfirmedIdentity entered{brand, model, productName};
    QString error;
    if (!saveIdentity(m_runner->result().item.itemId, entered, std::nullopt, &error)
        || !m_runner->setIdentity(entered, &error)) {
        setError(error);
        return false;
    }
    setError(QString());
    startSearches();
    return true;
}

void LookupSession::continueAfterUser()
{
    if (m_state != State::WaitingForUser || !m_controller)
        return;
    m_userMessage.clear();
    setState(m_stateBeforeUser);
    m_controller->continueAfterUser();
}

void LookupSession::cancel()
{
    if (!m_controller || m_state == State::Closing)
        return;
    qInfo().noquote() << "lookup: cancel pressed";
    if (!m_runner) {
        // The browser is still starting: nothing was read, nothing is saved.
        m_savedOnce = true;
        setState(State::Closing);
        m_controller->stop();
        return;
    }
    // While the browser waits for the user no page is being loaded: end that
    // wait (the runner then gets "failed: stopped").
    if (m_state == State::WaitingForUser && m_controller->isBusy()) {
        m_userMessage.clear();
        m_controller->cancel();
        return;
    }
    m_runner->cancel();
}

void LookupSession::onRunnerFinished()
{
    saveLookup(m_runner->result());
    setState(State::Closing);
    m_controller->stop();
}

void LookupSession::saveLookup(lookup::LookupResult result)
{
    if (m_savedOnce)
        return;
    m_savedOnce = true;
    m_step = StepSaving;
    emit progressChanged();
    const QList<lookup::Row> rows = lookup::classifyRows(result, *m_words);
    const lookup::Valuation valuation =
        lookup::valuate(result, rows, Settings(m_folder.settingsPath()).valueSettings());
    QString error;
    const std::optional<qint64> id = m_store->save(m_input, result, rows, valuation, m_depths, m_historyDays, &error);
    if (!id) {
        setError(QStringLiteral("The lookup could not be saved: %1").arg(error));
        return;
    }
    qInfo().noquote() << "lookup: saved as" << *id << "-" << result.status << "-" << rows.size() << "rows";
    emit saved(*id);
}

void LookupSession::onBrowserFinished(bool normal)
{
    qInfo().noquote() << "lookup:" << m_browserName << "closed" << (normal ? "(normal)" : "(abnormal)");
    // The browser ended the session itself: save what was collected.
    if (m_runner && !m_savedOnce) {
        lookup::LookupResult result = m_runner->result();
        result.status = QStringLiteral("stopped");
        result.message = m_browserError.isEmpty() ? QStringLiteral("the browser closed") : m_browserError;
        saveLookup(result);
    } else if (!m_runner && !m_savedOnce) {
        setError(QStringLiteral("%1 stopped before the lookup began: %2")
                     .arg(m_browserName, m_browserError.isEmpty() ? QStringLiteral("no reason given") : m_browserError));
    }
    m_controller->deleteLater();
    m_controller = nullptr;
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    m_userMessage.clear();
    setState(State::Idle);
    emit ended();
}

void LookupSession::fetchPhoto(const lookup::ItemRecord &item)
{
    const QUrl url(item.imageUrl, QUrl::StrictMode);
    if (item.itemId.isEmpty() || !url.isValid() || url.scheme() != QLatin1String("https") || !m_store->photo(item.itemId).isEmpty())
        return;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        // Never from https to http.
        m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    QNetworkRequest request(url);
    // No cookies sent or kept, no cache.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(kPhotoTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    const QString itemId = item.itemId;
    const QString address = item.imageUrl;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxPhotoBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId, address]() { onPhotoReply(reply, itemId, address); });
    qInfo().noquote() << "lookup: listing photo requested from" << url.host();
}

void LookupSession::onPhotoReply(QNetworkReply *reply, const QString &itemId, const QString &url)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qWarning().noquote() << "lookup: listing photo not loaded:" << reply->errorString();
        return;
    }
    QImage image;
    if (!image.loadFromData(reply->readAll())) {
        qWarning().noquote() << "lookup: listing photo not loaded: not an image the app can read";
        return;
    }
    if (image.width() > kPhotoSide || image.height() > kPhotoSide)
        image = image.scaled(kPhotoSide, kPhotoSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPG", kPhotoJpegQuality)) {
        qWarning().noquote() << "lookup: listing photo not saved: it cannot be stored as JPEG";
        return;
    }
    QString error;
    if (!m_store->savePhoto(itemId, url, jpeg, &error)) {
        qWarning().noquote() << "lookup: listing photo not saved:" << error;
        return;
    }
    emit photoSaved(itemId);
}

QString LookupSession::askTitle() const { return m_runner ? m_runner->result().item.title : QString(); }
QString LookupSession::askItemId() const { return m_runner ? m_runner->result().item.itemId : QString(); }
QString LookupSession::askReason() const { return m_runner ? m_runner->result().identity.reason : QString(); }

QStringList LookupSession::askSpecifics() const
{
    QStringList list;
    if (m_runner) {
        for (const lookup::ItemRecord::Specific &specific : m_runner->result().item.specifics)
            list.append(QStringLiteral("%1: %2").arg(specific.label, specific.value));
    }
    return list;
}

QStringList LookupSession::askSuggestions() const
{
    return m_runner ? m_runner->result().identity.candidates : QStringList();
}

QString LookupSession::askBrand() const { return m_runner ? m_runner->result().identity.brandName : QString(); }
QString LookupSession::askModel() const { return m_runner ? m_runner->result().identity.model : QString(); }
QString LookupSession::askProductName() const { return m_runner ? m_runner->result().identity.productName : QString(); }

} // namespace app::ui
