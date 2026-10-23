#pragma once

#include "core/WorkFolder.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupStore.h"
#include "lookup/MatchWords.h"
#include "model/ModelAssist.h"

#include <QJsonObject>
#include <QObject>

#include <functional>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace app::browser {
class BrowserController;
}
namespace app::lookup {
class LookupRunner;
}
namespace app::plugins {
class PluginHost;
}

namespace app::ui {

// One lookup from the user interface (inbox phase 6 step 4, PLAN 5):
//   start(input)       a pasted link: a browser session in the user's
//                      installed browser (BrowserController, its own profile
//                      in the work folder), the item page, the identity,
//                      then the searches
//   startCorrected()   "Wrong item? Correct it" on a saved lookup: the
//                      corrected identity is saved for the listing, then the
//                      saved listing record is identified again (the saved
//                      identity comes first) and searched; the item page is
//                      not loaded again
// With a confident identity the searches start at once with the queries
// built from it (one per brand search name, owner rules 21, 23) and the
// depths and period from Settings - or, when the user asked to review them
// (start(input, true)), state ReviewSearches shows the queries (editable, one
// per line) and this lookup's depths first (owner decisions Q14 "editable
// before the search" and answer 4 "depth ... per lookup"). Without one nothing is searched: state
// AskIdentity shows the listing's title, item specifics and a link to it,
// and the user enters brand, model and product name (submitIdentity(), rule
// 26). Every identity the user enters is saved to the known models in the
// work folder (KnownModels::saveConfirmed, reused next time, rule 7) and
// recorded in the database (LookupStore::saveUserIdentity).
//
// When eBay shows a page the app cannot read (sign-in, ship-to prompt, a
// check), the browser window comes to the front and state WaitingForUser
// shows the browser controller's message; the user answers it there and
// presses Continue (continueAfterUser()). Nothing is solved or skipped by
// the app.
//
// The lookup is saved when it ends - complete, cancelled (partial) or
// stopped (failed), with what was collected - in one transaction
// (LookupStore::save), then saved(id) is emitted and the browser is closed.
// When the browser ends the session itself (closed, crashed), or the app
// quits during a lookup, what was collected is saved as stopped. Only a
// session whose browser never got ready saves nothing.
//
// The listing's photo: after the item page is read, its JSON-LD photo
// address (https, host i.ebayimg.com only: the host the item pages gave,
// seen 2026-10-19) is downloaded once by the app itself (not through the
// browser; it is no eBay page load and not counted in the daily cap): no
// cookies are sent or kept, nothing is written to disk but the scaled JPEG
// in the database (LookupStore::savePhoto). Any failure leaves the lookup
// without a photo.
class LookupSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    // The state for QML, as a word: "idle", "starting", "reading",
    // "askIdentity", "searching", "waitingForUser", "closing".
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    // True from start() until the session is over (state Idle).
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    // The step being done (an index into `steps`).
    Q_PROPERTY(int step READ step NOTIFY progressChanged)
    Q_PROPERTY(QStringList steps READ steps CONSTANT)
    // eBay pages loaded and answered for this lookup.
    Q_PROPERTY(int pagesThisLookup READ pagesThisLookup NOTIFY progressChanged)
    Q_PROPERTY(QString lastLine READ lastLine NOTIFY progressChanged)
    Q_PROPERTY(QString browserName READ browserName NOTIFY stateChanged)
    // WaitingForUser: what to do in the browser window.
    Q_PROPERTY(QString userMessage READ userMessage NOTIFY stateChanged)
    // Why the last start(), submitIdentity() or session failed; empty when
    // nothing failed.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // AskIdentity: the listing and the suggestions.
    Q_PROPERTY(QString askTitle READ askTitle NOTIFY stateChanged)
    Q_PROPERTY(QString askItemId READ askItemId NOTIFY stateChanged)
    Q_PROPERTY(QString askReason READ askReason NOTIFY stateChanged)
    Q_PROPERTY(QStringList askSpecifics READ askSpecifics NOTIFY stateChanged)
    Q_PROPERTY(QStringList askSuggestions READ askSuggestions NOTIFY stateChanged)
    Q_PROPERTY(QString askBrand READ askBrand NOTIFY stateChanged)
    Q_PROPERTY(QString askModel READ askModel NOTIFY stateChanged)
    Q_PROPERTY(QString askProductName READ askProductName NOTIFY stateChanged)
    // The local model's identity when it differs from the rules' ("Brand
    // Model"), shown next to the rules' answer; empty otherwise.
    Q_PROPERTY(QString askModelAnswer READ askModelAnswer NOTIFY stateChanged)
    Q_PROPERTY(QString askModelAnswerBrand READ askModelAnswerBrand NOTIFY stateChanged)
    Q_PROPERTY(QString askModelAnswerModel READ askModelAnswerModel NOTIFY stateChanged)
    // ReviewSearches: the queries and this lookup's depths (from Settings).
    Q_PROPERTY(QStringList reviewQueries READ reviewQueries NOTIFY stateChanged)
    Q_PROPERTY(int reviewSoldDepth READ reviewSoldDepth NOTIFY stateChanged)
    Q_PROPERTY(int reviewActiveDepth READ reviewActiveDepth NOTIFY stateChanged)
    Q_PROPERTY(int reviewResearchDepth READ reviewResearchDepth NOTIFY stateChanged)
public:
    // Another part of the app holds the browser (the seller panel reading
    // reviews): no lookup starts while it returns true.
    void setBrowserBusyCheck(std::function<bool()> busy) { m_browserBusy = std::move(busy); }
    // Idle: no lookup. Starting: the browser starts. Reading: the listing
    // page. AskIdentity: the user enters the identity. ReviewSearches: the
    // user checks the queries and depths. Searching: the comparison pages. WaitingForUser: the user acts in the browser window.
    // Closing: saved, the browser closes.
    enum class State { Idle, Starting, Reading, AskIdentity, ReviewSearches, Searching, WaitingForUser, Closing };

    // `known`, `words` and `store` must outlive the session.
    LookupSession(WorkFolder folder, lookup::KnownModels *known, const lookup::MatchWords *words,
                  const lookup::LookupStore *store, QObject *parent = nullptr);
    ~LookupSession() override;

    // The enabled matcher plugins run on every lookup's rows before it is
    // saved (DESIGN 5). `host` may be null (no plugins); it must outlive the
    // session.
    void setPlugins(plugins::PluginHost *host) { m_plugins = host; }

    // False (and errorText) when a lookup runs, the input is not an eBay
    // item address or number (nothing is saved, owner decision Q21), no
    // supported browser is installed, or the browser cannot be started.
    // `reviewFirst`: stop at ReviewSearches before searching.
    bool start(const QString &input, bool reviewFirst = false);
    // False (and errorText) when a lookup runs, the identity is incomplete or
    // cannot be saved, or the browser cannot be started.
    bool startCorrected(const lookup::SavedLookup &saved, const QString &brand, const QString &model,
                        const QString &productName);

    // AskIdentity: the identity the user entered; the searches start. False
    // (and errorText) when brand or model is empty or it cannot be saved.
    Q_INVOKABLE bool submitIdentity(const QString &brand, const QString &model, const QString &productName);
    // ReviewSearches: searches `queries` (one per line, empty lines dropped)
    // with these depths for this lookup. False (and errorText) when no query
    // is left or a depth is negative.
    Q_INVOKABLE bool startReviewedSearches(const QString &queries, int soldDepth, int activeDepth, int researchDepth);
    // WaitingForUser: the user has done what userMessage asked for.
    Q_INVOKABLE void continueAfterUser();
    // Stops the lookup: after the page being loaded, at once while waiting
    // for the user or for the identity. What was collected is saved.
    Q_INVOKABLE void cancel();

    State state() const { return m_state; }
    QString stateName() const;
    bool isRunning() const { return m_state != State::Idle; }
    int step() const { return m_step; }
    QStringList steps() const;
    int pagesThisLookup() const { return m_pages; }
    QString lastLine() const { return m_lastLine; }
    QString browserName() const { return m_browserName; }
    QString userMessage() const { return m_userMessage; }
    QString errorText() const { return m_errorText; }
    QString askTitle() const;
    QString askItemId() const;
    QString askReason() const;
    QStringList askSpecifics() const;
    QStringList askSuggestions() const;
    QString askBrand() const;
    QString askModel() const;
    QString askProductName() const;
    QString askModelAnswer() const;
    QString askModelAnswerBrand() const { return askModelAnswer().isEmpty() ? QString() : m_modelIdentity->answer.brand; }
    QString askModelAnswerModel() const { return askModelAnswer().isEmpty() ? QString() : m_modelIdentity->answer.model; }
    QStringList reviewQueries() const;
    int reviewSoldDepth() const { return m_depths.sold; }
    int reviewActiveDepth() const { return m_depths.active; }
    int reviewResearchDepth() const { return m_depths.research; }

signals:
    void stateChanged();
    void progressChanged();
    void errorChanged();
    // The lookup was saved as `lookupId`.
    void saved(qint64 lookupId);
    // A listing photo was saved.
    void photoSaved(const QString &itemId);
    // The session is over (state Idle again); the page count changed.
    void ended();

private:
    std::function<bool()> m_browserBusy;
    bool startSession(const QString &input, const std::optional<lookup::ItemRecord> &item, bool reviewFirst);
    void setState(State state);
    void setError(const QString &error);
    void onReady();
    void onItemReady();
    // The searches with the built queries and m_depths, or ReviewSearches
    // first.
    void searchOrReview();
    void startSearches(const QStringList &queries);
    void onRunnerFinished();
    void onBrowserFinished(bool normal);
    // `lookupId`: the saved lookup being corrected, if any.
    bool saveIdentity(const QString &itemId, const lookup::ConfirmedIdentity &identity, std::optional<qint64> lookupId,
                      QString *error);
    // Rows by the rules (and the matcher plugins), then finishSave().
    void saveLookup(lookup::LookupResult result);
    QList<lookup::Row> classifyAndMatch(const lookup::LookupResult &result) const;
    // The own-listing check, the numbers, the local model's notes, the save.
    void finishSave(lookup::LookupResult result, QList<lookup::Row> rows);
    void onModelIdentity(const model::ModelAssist::IdentityOutcome &outcome);
    void onModelSorted(const QList<lookup::Row> &rows, const QString &note);
    // The rows are final: the Deep check when it is on (Settings), then
    // finishSave() and the browser's close.
    void proceedToSave(lookup::LookupResult result, QList<lookup::Row> rows);
    void deepCheckNext();
    void onDeepReply(const QJsonObject &reply);
    void endDeepCheck(const QString &stopNote);
    void fetchPhoto(const lookup::ItemRecord &item);
    void onPhotoReply(QNetworkReply *reply, const QString &itemId, const QString &url);

    WorkFolder m_folder;
    lookup::KnownModels *m_known;
    const lookup::MatchWords *m_words;
    const lookup::LookupStore *m_store;
    plugins::PluginHost *m_plugins = nullptr;
    browser::BrowserController *m_controller = nullptr;
    lookup::LookupRunner *m_runner = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    State m_state = State::Idle;
    State m_stateBeforeUser = State::Idle;
    QString m_input;
    std::optional<lookup::ItemRecord> m_savedItem;
    lookup::Depths m_depths;
    int m_historyDays = 0;
    bool m_savedOnce = false;
    bool m_reviewFirst = false;
    QString m_browserError;
    int m_step = 0;
    int m_pages = 0;
    QString m_lastLine;
    QString m_browserName;
    QString m_userMessage;
    QString m_errorText;

    // The local model (DESIGN 3.6): used in this lookup, its notes for the
    // saved lookup, its identity answer while the user is asked, and the
    // lookup's result while its rows are sorted.
    model::ModelAssist *m_assist;
    bool m_useModel = false;
    bool m_identityPending = false;
    bool m_modelSorting = false;
    bool m_browserEnded = false;
    QStringList m_modelNotes;
    std::optional<model::ModelAssist::IdentityOutcome> m_modelIdentity;
    lookup::LookupResult m_sortingResult;

    // The Deep check (owner decision f3, 2026-10-21): the rows being checked,
    // the row indexes still to open, the one open now, and what it did.
    bool m_deepChecking = false;
    bool m_cancelPressed = false; // no Deep check after Cancel
    int m_deepOpened = 0;
    int m_deepPagesBefore = 0; // the day's page count when the check began
    lookup::LookupResult m_deepResult;
    QList<lookup::Row> m_deepRows;
    QList<qsizetype> m_deepQueue;
    qsizetype m_deepCurrent = -1;
    int m_deepTotal = 0;
    int m_deepMoved = 0;
    int m_deepNoted = 0;
    int m_deepUnread = 0;
    int m_deepDropped = 0; // removed listings without a description: not counted
    QString m_deepStop; // why the check ended early, or empty
};

} // namespace app::ui
