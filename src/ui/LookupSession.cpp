#include "ui/LookupSession.h"

#include "browser/BrowserCatalog.h"
#include "browser/BrowserController.h"
#include "core/Settings.h"
#include "ebay/SearchUrls.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"
#include "plugins/PluginHost.h"

#include <QBuffer>
#include <QCoreApplication>
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
    // The local model's runner is this program with "--model-runner".
    model::ModelAssist::Options options;
    options.modelsDir = m_folder.modelsDir();
    options.connectionName = m_store->connectionName();
    options.runnerProgram = QCoreApplication::applicationFilePath();
    options.runnerArguments = {QStringLiteral("--model-runner")};
    m_assist = new model::ModelAssist(options, m_known, m_words, this);
    connect(m_assist, &model::ModelAssist::identityDone, this, &LookupSession::onModelIdentity);
    connect(m_assist, &model::ModelAssist::sortDone, this, &LookupSession::onModelSorted);
    connect(m_assist, &model::ModelAssist::progressChanged, this, [this] {
        if (!m_assist->progressLine().isEmpty()) {
            m_lastLine = m_assist->progressLine();
            emit progressChanged();
        }
    });
}

// The app quits during a lookup: what was collected is saved as stopped
// (the database is still open: the Backend deletes the session first).
LookupSession::~LookupSession()
{
    if (m_runner && !m_savedOnce) {
        lookup::LookupResult result = m_runner->result();
        result.status = QStringLiteral("stopped");
        result.message = QStringLiteral("the app was closed during the lookup");
        saveLookup(result);
    }
}

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
    case State::ReviewSearches:
        return QStringLiteral("reviewSearches");
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

bool LookupSession::start(const QString &input, bool reviewFirst)
{
    if (isRunning()) {
        setError(QStringLiteral("A lookup is already running."));
        return false;
    }
    if (ebay::itemIdFromInput(input).isEmpty()) {
        setError(QStringLiteral("This is not an eBay listing link (https://www.ebay.com/itm/...) or a 12-digit item number."));
        return false;
    }
    return startSession(input.trimmed(), std::nullopt, reviewFirst);
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
    return startSession(saved.input, saved.result.item, false);
}

bool LookupSession::startSession(const QString &input, const std::optional<lookup::ItemRecord> &item, bool reviewFirst)
{
    if (m_browserBusy && m_browserBusy()) {
        setError(QStringLiteral("The seller panel is reading a seller's reviews; check again when it has finished."));
        return false;
    }
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
    m_reviewFirst = reviewFirst;
    m_savedOnce = false;
    m_browserError.clear();
    m_browserName = chosen->name;
    m_step = StepBrowser;
    m_pages = 0;
    m_lastLine = QStringLiteral("Starting %1...").arg(chosen->name);
    m_userMessage.clear();
    setError(QString());
    // The local model, when it is on (DESIGN 3.6): used only when it can be
    // (downloaded, enough memory); otherwise the lookup says why.
    m_modelNotes.clear();
    m_modelIdentity.reset();
    m_identityPending = false;
    m_modelSorting = false;
    m_browserEnded = false;
    const model::ModelAssist::Config modelConfig{
        settings.modelEnabled(), settings.modelId().isEmpty() ? model::defaultModelId() : settings.modelId(), settings.modelGpu()};
    QString modelWhy;
    m_useModel = m_assist->usable(modelConfig, &modelWhy);
    if (m_useModel)
        m_assist->begin(modelConfig);
    else if (!modelWhy.isEmpty())
        m_modelNotes.append(QStringLiteral("Local model not used: %1.").arg(modelWhy));
    connect(m_controller, &browser::BrowserController::ready, this, &LookupSession::onReady);
    connect(m_controller, &browser::BrowserController::done, this, [this](const QJsonObject &reply) {
        if (m_deepChecking) {
            onDeepReply(reply);
            return;
        }
        if (!m_runner)
            return;
        m_runner->onReply(reply);
        // Pages the browser loaded and answered for (a load the daily cap
        // refused is not one).
        if (m_runner && m_pages != m_runner->result().pagesLoaded) {
            m_pages = m_runner->result().pagesLoaded;
            emit progressChanged();
        }
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
            case ebay::PageKind::Feedback:
            case ebay::PageKind::Purchases:
                // Not part of a lookup (the seller panel reads them).
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
    // From the event loop after startFromItem(): the session may be over.
    if (!m_runner || m_state == State::Closing)
        return;
    const lookup::LookupResult &result = m_runner->result();
    if (!m_savedItem)
        fetchPhoto(result.item);
    // The local model's second opinion (G7: never on the user's, a saved or
    // a known model's identity); the lookup goes on in onModelIdentity().
    if (m_useModel && model::mayAskIdentity(result.identity)) {
        m_identityPending = true;
        m_assist->checkIdentity(result.item, result.identity);
        return;
    }
    if (result.identity.canSearch()) {
        searchOrReview();
        return;
    }
    // Rule 26: no confident model, no search; the user enters it.
    setState(State::AskIdentity);
}

void LookupSession::onModelIdentity(const model::ModelAssist::IdentityOutcome &outcome)
{
    if (!m_identityPending || !m_runner || m_state == State::Closing)
        return;
    m_identityPending = false;
    m_assist->end();
    if (!outcome.note.isEmpty())
        m_modelNotes.append(outcome.note);
    using Kind = model::ModelAssist::IdentityOutcome::Kind;
    // G10: a different identity is never searched without the user; the
    // model's own suggestion only fills the question.
    if (outcome.kind == Kind::Disagrees || outcome.kind == Kind::Suggests) {
        m_modelIdentity = outcome;
        setState(State::AskIdentity);
        return;
    }
    if (m_runner->result().identity.canSearch())
        searchOrReview();
    else
        setState(State::AskIdentity);
}

void LookupSession::searchOrReview()
{
    if (m_reviewFirst)
        setState(State::ReviewSearches);
    else
        startSearches(m_runner->result().queries);
}

void LookupSession::startSearches(const QStringList &queries)
{
    QString error;
    const State before = m_state;
    if (!m_runner->startComps(queries, m_depths, m_historyDays, &error)) {
        setError(error);
        setState(before == State::ReviewSearches ? State::ReviewSearches : State::AskIdentity);
        return;
    }
    // With every depth 0 the lookup is already finished (and Closing) here.
    if (m_state == before)
        setState(State::Searching);
}

bool LookupSession::startReviewedSearches(const QString &queries, int soldDepth, int activeDepth, int researchDepth)
{
    if (m_state != State::ReviewSearches || !m_runner)
        return false;
    if (soldDepth < 0 || activeDepth < 0 || researchDepth < 0) {
        setError(QStringLiteral("A depth cannot be negative (0 skips that source)."));
        return false;
    }
    QStringList lines;
    for (const QString &line : queries.split(QLatin1Char('\n'))) {
        if (!line.simplified().isEmpty())
            lines.append(line.simplified());
    }
    if (lines.isEmpty()) {
        setError(QStringLiteral("Enter at least one search, one per line."));
        return false;
    }
    m_depths = {soldDepth, activeDepth, researchDepth};
    setError(QString());
    startSearches(lines);
    return m_state != State::ReviewSearches;
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
    m_modelIdentity.reset();
    searchOrReview();
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
    // The Deep check: no more rows; what it read is kept and saved.
    if (m_deepChecking) {
        if (m_state == State::WaitingForUser && m_controller->isBusy()) {
            m_userMessage.clear();
            m_controller->cancel(); // its reply ends the row, then the check
        }
        m_deepQueue.clear();
        return;
    }
    // The local model's rows: stop it; what it checked is kept (sortDone).
    if (m_modelSorting) {
        m_assist->cancel();
        return;
    }
    // Its identity question: dropped; the runner's cancel below ends the
    // lookup.
    if (m_identityPending) {
        m_identityPending = false;
        m_assist->cancel();
    }
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
    // The local model sorts the rows before they are saved (not after a
    // cancel: the user wants it to end).
    const lookup::LookupResult &result = m_runner->result();
    if (m_useModel && !m_savedOnce && result.status != QLatin1String("cancelled")) {
        QList<lookup::Row> rows = classifyAndMatch(result);
        if (!rows.isEmpty()) {
            m_sortingResult = result;
            m_modelSorting = true;
            m_step = StepSaving;
            emit progressChanged();
            m_assist->sortRows(m_sortingResult, rows);
            return;
        }
    }
    if (result.status == QLatin1String("complete") && !m_savedOnce) {
        proceedToSave(result, classifyAndMatch(result));
        return;
    }
    saveLookup(result);
    setState(State::Closing);
    m_controller->stop();
}

void LookupSession::onModelSorted(const QList<lookup::Row> &rows, const QString &note)
{
    if (!m_modelSorting)
        return;
    m_modelSorting = false;
    m_modelNotes.append(note);
    m_assist->end();
    proceedToSave(m_sortingResult, rows);
}

void LookupSession::proceedToSave(lookup::LookupResult result, QList<lookup::Row> rows)
{
    const Settings settings(m_folder.settingsPath());
    if (settings.deepCheck() && result.status == QLatin1String("complete") && !m_savedOnce && !m_browserEnded && m_controller
        && m_words) {
        // The counted same-item rows of the searches (not the listing itself):
        // sold ones first, then for sale; each group cheapest first.
        QList<qsizetype> sold;
        QList<qsizetype> forSale;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            const lookup::Row &row = rows.at(i);
            if (row.match.bucket != lookup::Bucket::SameItem || !row.included || row.source == lookup::Row::Source::Research)
                continue;
            const QString itemId = lookup::rowItemId(result, row);
            if (itemId.isEmpty() || itemId == result.item.itemId)
                continue;
            (row.source == lookup::Row::Source::Sold ? sold : forSale).append(i);
        }
        const auto cheapestFirst = [&](qsizetype a, qsizetype b) {
            const std::optional<qint64> totalA = lookup::rowTotal(result, rows.at(a));
            const std::optional<qint64> totalB = lookup::rowTotal(result, rows.at(b));
            if (totalA.has_value() != totalB.has_value())
                return totalA.has_value();
            return totalA && *totalA < *totalB;
        };
        std::stable_sort(sold.begin(), sold.end(), cheapestFirst);
        std::stable_sort(forSale.begin(), forSale.end(), cheapestFirst);
        QList<qsizetype> queue = sold + forSale;
        queue.resize(std::min<qsizetype>(queue.size(), settings.deepCheckRows()));
        if (!queue.isEmpty()) {
            m_deepChecking = true;
            m_deepResult = std::move(result);
            m_deepRows = std::move(rows);
            m_deepQueue = queue;
            m_deepTotal = static_cast<int>(queue.size());
            m_deepMoved = m_deepNoted = m_deepUnread = 0;
            m_deepResult.deepCheckPages = 0;
            qInfo().noquote() << "lookup: deep check of" << m_deepTotal << "rows";
            deepCheckNext();
            return;
        }
    }
    finishSave(std::move(result), std::move(rows));
    if (!m_browserEnded && m_controller) {
        setState(State::Closing);
        m_controller->stop();
    }
}

void LookupSession::deepCheckNext()
{
    if (m_deepQueue.isEmpty()) {
        endDeepCheck(QString());
        return;
    }
    m_deepCurrent = m_deepQueue.takeFirst();
    const QString itemId = lookup::rowItemId(m_deepResult, m_deepRows.at(m_deepCurrent));
    m_lastLine = QStringLiteral("Deep check: row %1 of %2 (item %3)")
                     .arg(m_deepTotal - static_cast<int>(m_deepQueue.size()))
                     .arg(m_deepTotal)
                     .arg(itemId);
    emit progressChanged();
    m_controller->fetch(QUrl(QStringLiteral("https://www.ebay.com/itm/") + itemId), ebay::PageKind::Item);
}

void LookupSession::onDeepReply(const QJsonObject &reply)
{
    const QString status = reply.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("limit_reached")) {
        endDeepCheck(QStringLiteral("stopped at the daily page cap"));
        return;
    }
    if (status == QLatin1String("failed")) {
        endDeepCheck(QStringLiteral("stopped: %1").arg(reply.value(QStringLiteral("error")).toString()));
        return;
    }
    ++m_deepResult.deepCheckPages;
    lookup::Row &row = m_deepRows[m_deepCurrent];
    const QString itemId = lookup::rowItemId(m_deepResult, row);
    if (status != QLatin1String("ok")) {
        ++m_deepUnread;
        qInfo().noquote() << "lookup: deep check" << itemId << "- not an item page:" << reply.value(QStringLiteral("reason")).toString();
        deepCheckNext();
        return;
    }
    const lookup::ItemRecord item = lookup::ItemRecord::fromJson(reply.value(QStringLiteral("result")).toObject());
    if (item.description.isEmpty()) {
        ++m_deepUnread;
        qInfo().noquote() << "lookup: deep check" << itemId << "- no description:" << item.descriptionMissing;
        deepCheckNext();
        return;
    }
    const lookup::DescriptionFinding finding = lookup::descriptionFinding(item.description, m_deepResult.identity, *m_words);
    if (finding.bucket != lookup::Bucket::SameItem) {
        row.match.bucket = finding.bucket;
        row.included = false;
        row.match.reasons.append(finding.reason + QStringLiteral(" (deep check)"));
        ++m_deepMoved;
    } else if (!finding.note.isEmpty()) {
        row.match.reasons.append(finding.note + QStringLiteral(" - still counted (deep check)"));
        ++m_deepNoted;
    }
    qInfo().noquote() << "lookup: deep check" << itemId << "-"
                      << (finding.bucket != lookup::Bucket::SameItem ? finding.reason
                                                                       : finding.note.isEmpty() ? QStringLiteral("description read, no change")
                                                                                                : finding.note);
    deepCheckNext();
}

void LookupSession::endDeepCheck(const QString &stopNote)
{
    m_deepChecking = false;
    QString note = QStringLiteral("Deep check: %1 of %2 rows opened (%3 eBay pages); %4 moved by their description, %5 noted, "
                                  "%6 without a readable description")
                       .arg(m_deepTotal - static_cast<int>(m_deepQueue.size()))
                       .arg(m_deepTotal)
                       .arg(m_deepResult.deepCheckPages)
                       .arg(m_deepMoved)
                       .arg(m_deepNoted)
                       .arg(m_deepUnread);
    if (!stopNote.isEmpty())
        note += QStringLiteral("; ") + stopNote;
    qInfo().noquote() << "lookup:" << note;
    m_deepResult.notes.append(note + QLatin1Char('.'));
    m_deepQueue.clear();
    finishSave(std::move(m_deepResult), std::move(m_deepRows));
    if (!m_browserEnded && m_controller) {
        setState(State::Closing);
        m_controller->stop();
    }
}

QList<lookup::Row> LookupSession::classifyAndMatch(const lookup::LookupResult &result) const
{
    QList<lookup::Row> rows = lookup::classifyRows(result, *m_words);
    // The built-in matcher first, then the matcher plugins on its rows.
    if (m_plugins)
        m_plugins->applyMatchers(result, &rows);
    return rows;
}

void LookupSession::saveLookup(lookup::LookupResult result)
{
    if (m_savedOnce)
        return;
    QList<lookup::Row> rows = classifyAndMatch(result);
    finishSave(std::move(result), std::move(rows));
}

void LookupSession::finishSave(lookup::LookupResult result, QList<lookup::Row> rows)
{
    if (m_savedOnce)
        return;
    m_savedOnce = true;
    m_step = StepSaving;
    emit progressChanged();
    result.notes.append(m_modelNotes);
    // The pasted listing is never "not this item" (owner case 11).
    if (!lookup::checkOwnListing(&result, &rows, *m_words))
        qWarning().noquote() << "lookup: the identity does not match the listing's own title; no numbers";
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
    m_browserEnded = true;
    // The browser closed during the Deep check: what it read is saved now.
    if (m_deepChecking)
        endDeepCheck(QStringLiteral("stopped: the browser closed"));
    // The browser closed while the local model sorted: what it checked is
    // saved now (onModelSorted).
    if (m_modelSorting)
        m_assist->cancel();
    m_identityPending = false;
    m_assist->end();
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
    if (item.itemId.isEmpty() || !m_store->photo(item.itemId).isEmpty())
        return;
    // Only eBay's image host (the one the item pages gave, seen 2026-10-19):
    // the app asks no other server for anything.
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String("i.ebayimg.com")) {
        qWarning().noquote() << "lookup: listing photo not loaded: its address is not on i.ebayimg.com";
        return;
    }
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        // Redirects only within the same scheme, host and port.
        m_network->setRedirectPolicy(QNetworkRequest::SameOriginRedirectPolicy);
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

QStringList LookupSession::reviewQueries() const
{
    return m_runner ? m_runner->result().queries : QStringList();
}

QString LookupSession::askTitle() const { return m_runner ? m_runner->result().item.title : QString(); }
QString LookupSession::askItemId() const { return m_runner ? m_runner->result().item.itemId : QString(); }
QString LookupSession::askReason() const
{
    if (!m_runner)
        return {};
    const lookup::Identity &identity = m_runner->result().identity;
    if (m_modelIdentity && m_modelIdentity->kind == model::ModelAssist::IdentityOutcome::Kind::Disagrees)
        return QStringLiteral("The rules read %1 %2, the local model %3 %4. Check which is right, or correct it.")
            .arg(identity.brandName, identity.model, m_modelIdentity->answer.brand, m_modelIdentity->answer.model);
    if (m_modelIdentity && m_modelIdentity->kind == model::ModelAssist::IdentityOutcome::Kind::Suggests)
        return identity.reason + QStringLiteral(" The fields hold the local model's reading; check it.");
    return identity.reason;
}

QString LookupSession::askModelAnswer() const
{
    if (!m_modelIdentity || m_modelIdentity->kind != model::ModelAssist::IdentityOutcome::Kind::Disagrees)
        return {};
    return m_modelIdentity->answer.brand + QLatin1Char(' ') + m_modelIdentity->answer.model;
}

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
    if (!m_runner)
        return {};
    QStringList candidates = m_runner->result().identity.candidates;
    // The local model's model code (guarded), first, when it gave one.
    if (m_modelIdentity && !m_modelIdentity->answer.model.isEmpty()) {
        candidates.removeAll(m_modelIdentity->answer.model);
        candidates.prepend(m_modelIdentity->answer.model);
    }
    return candidates;
}

QString LookupSession::askBrand() const
{
    if (!m_runner)
        return {};
    // Not confident: the local model's (guarded) reading fills the field.
    if (m_modelIdentity && m_modelIdentity->kind == model::ModelAssist::IdentityOutcome::Kind::Suggests)
        return m_modelIdentity->answer.brand;
    return m_runner->result().identity.brandName;
}
// The model found, else the best suggestion (owner case 12: the field is
// pre-filled with the best candidate).
QString LookupSession::askModel() const
{
    if (!m_runner)
        return {};
    if (m_modelIdentity && m_modelIdentity->kind == model::ModelAssist::IdentityOutcome::Kind::Suggests)
        return m_modelIdentity->answer.model;
    const lookup::Identity &identity = m_runner->result().identity;
    return identity.model.isEmpty() ? identity.candidates.value(0) : identity.model;
}
QString LookupSession::askProductName() const { return m_runner ? m_runner->result().identity.productName : QString(); }

} // namespace app::ui
