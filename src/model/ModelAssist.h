#pragma once

#include "lookup/Identity.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"
#include "lookup/MatchWords.h"
#include "model/Guards.h"
#include "model/ModelClient.h"
#include "model/ModelStore.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace app::model {

// The local model's part in one lookup (DESIGN 3.6, item 4): the identity
// question after the listing is read, the sorting question for the rows
// before the lookup is saved. Answers come from the cache when the same
// question was asked before, else from the runner (started on the first
// question, ended by end()). Every answer goes through the guards; the
// outcome is what the guards allow and a one-line note for the lookup.
class ModelAssist : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString modelsDir;
        QString connectionName; // the database (answer cache)
        QString runnerProgram;  // the app; tests: a fake runner
        QStringList runnerArguments;
        ModelClient::Limits limits;
        int sortBudgetMs = 3 * 60 * 1000;
        int maxRows = 400;
    };
    struct Config {
        bool enabled = false;
        QString modelId;
        bool gpu = true;
    };

    struct IdentityOutcome {
        enum class Kind {
            Agrees,    // the rules' confident identity, confirmed
            Disagrees, // the rules are confident, the model names another: ask (G10)
            Suggests,  // the rules are not confident: the model's answer is a suggestion
            Refused,   // a guard refused the answer: no answer
            NoAnswer,  // no answer (runner failure)
        };
        Kind kind = Kind::NoAnswer;
        IdentityAnswer answer;
        QString note;
    };

    ModelAssist(Options options, const lookup::KnownModels *known, const lookup::MatchWords *words,
                QObject *parent = nullptr);
    ~ModelAssist() override;

    // Whether the model can be used with `config`: on, a model of the catalog,
    // downloaded, and the computer has at least twice its working memory (a
    // project rule: the browser and the app need memory too). *why says why
    // not when it is on but cannot be used (empty when it is off).
    bool usable(const Config &config, QString *why) const;

    // Starts one lookup's use of the model (usable() must be true).
    void begin(const Config &config);
    // identityDone() follows.
    void checkIdentity(const lookup::ItemRecord &item, const lookup::Identity &rules);
    // sortDone() follows with the rows as the guards allow.
    void sortRows(const lookup::LookupResult &result, const QList<lookup::Row> &rows);
    // Stops the running step: sortDone() follows with the rows so far.
    void cancel();
    // Ends the lookup's use: the runner is ended.
    void end();

    bool busy() const { return m_step != Step::Idle; }
    // "Sorting 120 rows with the local model (34 done)".
    QString progressLine() const { return m_progressLine; }

signals:
    void identityDone(const app::model::ModelAssist::IdentityOutcome &outcome);
    void sortDone(const QList<app::lookup::Row> &rows, const QString &note);
    void progressChanged();

private:
    enum class Step { Idle, Identity, Sort };

    // Asks (from the cache or the runner); onAnswer(text) or onFailure(error).
    void ask(const Prompt &prompt, Grammar grammar, const QString &version);
    void onLoaded(bool ok, const QString &error);
    void onAnswered(const QString &text, qint64 ms);
    void onFailed(const QString &error, bool ended);
    void deliver(const QString &text);
    void identityAnswer(const std::optional<QString> &text, const QString &error);
    void sortNext();
    void sortAnswer(const std::optional<QString> &text, const QString &error, bool runnerEnded);
    void finishSort(const QString &stopNote);
    QString titleOf(const lookup::Row &row) const;
    QString itemIdOf(const lookup::Row &row) const;
    void setProgress(const QString &line);

    Options m_options;
    const lookup::KnownModels *m_known;
    const lookup::MatchWords *m_words;
    ModelStore m_store;
    ModelClient *m_client;
    Config m_config;
    std::optional<ModelInfo> m_info;
    Step m_step = Step::Idle;
    // The question waiting for the runner.
    Prompt m_prompt;
    Grammar m_grammar = Grammar::Sort;
    QString m_key;
    QString m_version;
    QString m_progressLine;
    // Why the runner failed in this lookup (no second start), else empty.
    QString m_runnerError;

    // Identity.
    lookup::ItemRecord m_item;
    lookup::Identity m_rules;

    // Sorting.
    lookup::LookupResult m_result;
    QList<lookup::Row> m_rows;
    QList<qsizetype> m_queue; // indexes into m_rows still to ask
    qsizetype m_asked = 0;
    qsizetype m_total = 0;
    int m_moved = 0;
    int m_suggested = 0;
    int m_refused = 0;
    int m_cached = 0;
    QElapsedTimer m_sortTimer;
    // The current question, for its log line (owner decision e, 2026-10-21).
    QElapsedTimer m_callClock;
    bool m_fromCache = false;
};

} // namespace app::model
