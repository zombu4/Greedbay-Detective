#include "model/ModelAssist.h"

#include "model/ModelDownload.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QTimer>

namespace app::model {

namespace {

// One line per question to the model in the work folder's log (owner
// decision e, 2026-10-21): the prompt version, the listing's or the row's
// item number (never title text), the answer's fields, the guard that
// refused it and why, what the app did with it, and the time taken.
Q_STATIC_LOGGING_CATEGORY(lcModel, "model")

QList<QPair<QString, QString>> specificsOf(const lookup::ItemRecord &item)
{
    QList<QPair<QString, QString>> list;
    for (const lookup::ItemRecord::Specific &specific : item.specifics)
        list.append({specific.label, specific.value});
    return list;
}

} // namespace

ModelAssist::ModelAssist(Options options, const lookup::KnownModels *known, const lookup::MatchWords *words, QObject *parent)
    : QObject(parent),
      m_options(std::move(options)),
      m_known(known),
      m_words(words),
      m_store(m_options.connectionName),
      m_client(new ModelClient(m_options.runnerProgram, m_options.runnerArguments, m_options.limits, this))
{
    connect(m_client, &ModelClient::loaded, this, &ModelAssist::onLoaded);
    connect(m_client, &ModelClient::answered, this, &ModelAssist::onAnswered);
    connect(m_client, &ModelClient::failed, this, &ModelAssist::onFailed);
}

ModelAssist::~ModelAssist() { m_client->stop(); }

bool ModelAssist::usable(const Config &config, QString *why) const
{
    why->clear();
    if (!config.enabled)
        return false;
    const std::optional<ModelInfo> info = modelById(config.modelId);
    if (!info) {
        *why = QStringLiteral("the chosen model is not one the app offers");
        return false;
    }
    QString missing;
    if (!processorSupportsEngine(&missing)) {
        *why = QStringLiteral("this processor lacks %1, which the engine needs").arg(missing);
        return false;
    }
    if (!ModelDownload::isDownloaded(m_options.modelsDir, *info)) {
        *why = QStringLiteral("%1 is not downloaded (Settings)").arg(info->name);
        return false;
    }
    const qint64 need = 2 * (info->memoryBytes > 0 ? info->memoryBytes : info->sizeBytes);
    const qint64 have = physicalMemoryBytes();
    if (have > 0 && have < need) {
        *why = QStringLiteral("this computer has %1 GB of memory; %2 needs at least %3 GB")
                   .arg(static_cast<double>(have) / 1e9, 0, 'f', 1)
                   .arg(info->name)
                   .arg(static_cast<double>(need) / 1e9, 0, 'f', 1);
        return false;
    }
    return true;
}

void ModelAssist::begin(const Config &config)
{
    m_config = config;
    m_info = modelById(config.modelId);
    m_runnerError.clear();
}

void ModelAssist::end()
{
    m_client->stop();
    m_step = Step::Idle;
    m_progressLine.clear();
}

void ModelAssist::setProgress(const QString &line)
{
    m_progressLine = line;
    emit progressChanged();
}

void ModelAssist::ask(const Prompt &prompt, Grammar grammar, const QString &version)
{
    m_prompt = prompt;
    m_grammar = grammar;
    m_version = version;
    m_key = ModelStore::questionKey(m_info->id, version, prompt);
    m_callClock.start();
    m_fromCache = false;
    if (const std::optional<QString> cached = m_store.answer(m_key)) {
        m_fromCache = true;
        // From the event loop, as an answer from the runner would come.
        QTimer::singleShot(0, this, [this, text = *cached] {
            if (m_step == Step::Sort)
                ++m_cached;
            deliver(text);
        });
        return;
    }
    if (!m_runnerError.isEmpty()) {
        // The runner failed earlier in this lookup: no second try.
        QTimer::singleShot(0, this, [this] { onFailed(m_runnerError, true); });
        return;
    }
    if (!m_client->isRunning()) {
        setProgress(QStringLiteral("Loading the local model (%1)...").arg(m_info->name));
        m_client->start(ModelDownload::filePath(m_options.modelsDir, *m_info), *m_info, m_config.gpu);
        return; // onLoaded() asks
    }
    m_client->ask(prompt, grammar);
}

void ModelAssist::onLoaded(bool ok, const QString &error)
{
    if (m_step == Step::Idle)
        return;
    if (!ok) {
        onFailed(error, true);
        return;
    }
    // The question's time starts now, without the model's loading.
    m_callClock.start();
    m_client->ask(m_prompt, m_grammar);
}

void ModelAssist::onAnswered(const QString &text, qint64)
{
    if (m_step == Step::Idle)
        return;
    QString error;
    if (!m_store.save(m_key, m_info->id, m_version, text.left(2000), &error))
        qWarning().noquote() << "local model: the answer could not be cached:" << error;
    deliver(text);
}

void ModelAssist::deliver(const QString &text)
{
    if (m_step == Step::Identity)
        identityAnswer(text, {});
    else if (m_step == Step::Sort)
        sortAnswer(text, {}, false);
}

void ModelAssist::onFailed(const QString &error, bool ended)
{
    if (ended)
        m_runnerError = error;
    if (m_step == Step::Identity)
        identityAnswer(std::nullopt, error);
    else if (m_step == Step::Sort)
        sortAnswer(std::nullopt, error, ended);
}

void ModelAssist::checkIdentity(const lookup::ItemRecord &item, const lookup::Identity &rules)
{
    m_step = Step::Identity;
    m_item = item;
    m_rules = rules;
    setProgress(QStringLiteral("Checking the identity with the local model"));
    ask(identityPrompt(m_info->format, item.title, specificsOf(item)), Grammar::Identity, QString::fromLatin1(kIdentityVersion));
}

void ModelAssist::identityAnswer(const std::optional<QString> &text, const QString &error)
{
    m_step = Step::Idle;
    setProgress(QString());
    IdentityOutcome outcome;
    const auto logLine = [this](const QString &answer, const QString &guard, const QString &result) {
        qCInfo(lcModel).noquote() << QStringLiteral("%1 listing %2: %3; %4; result: %5; %6 ms%7")
                                         .arg(m_version, m_item.itemId.isEmpty() ? QStringLiteral("-") : m_item.itemId, answer,
                                              guard.isEmpty() ? QStringLiteral("no guard refused it") : guard, result)
                                         .arg(m_callClock.elapsed())
                                         .arg(m_fromCache ? QStringLiteral(" (saved answer)") : QString());
    };
    if (!text) {
        logLine(QStringLiteral("no answer (%1)").arg(error), {}, QStringLiteral("the rules' answer is used"));
        outcome.kind = IdentityOutcome::Kind::NoAnswer;
        outcome.note = QStringLiteral("Local model: no identity answer (%1); the rules' answer is used.").arg(error);
        emit identityDone(outcome);
        return;
    }
    const std::optional<IdentityAnswer> answer = parseIdentity(*text);
    if (!answer) {
        logLine(QStringLiteral("answer not readable"), QStringLiteral("G1: not the answer format"),
                QStringLiteral("refused, the rules' answer is used"));
        outcome.kind = IdentityOutcome::Kind::Refused;
        outcome.note = QStringLiteral("Local model: its identity answer was not readable (G1); the rules' answer is used.");
        emit identityDone(outcome);
        return;
    }
    const Verdict verdict = model::checkIdentity(*answer, m_item, *m_known, *m_words);
    outcome.answer = *answer;
    if (verdict.accepted)
        outcome.answer.model = verdict.model;
    if (!verdict.accepted) {
        outcome.kind = IdentityOutcome::Kind::Refused;
        outcome.note = QStringLiteral("Local model: \"%1 %2\" not used (%3: %4); the rules' answer is used.")
                           .arg(answer->brand, answer->model, verdict.guard, verdict.why);
    } else if (!m_rules.canSearch()) {
        outcome.kind = IdentityOutcome::Kind::Suggests;
        outcome.note = QStringLiteral("Local model suggested \"%1 %2\" when you were asked for the identity.").arg(answer->brand, answer->model);
    } else if (sameIdentity(*answer, m_rules, *m_known)) {
        outcome.kind = IdentityOutcome::Kind::Agrees;
        outcome.note = QStringLiteral("Local model agrees with the identity (%1 %2).").arg(m_rules.brandName, m_rules.model);
    } else {
        outcome.kind = IdentityOutcome::Kind::Disagrees;
        outcome.note = QStringLiteral("Local model read \"%1 %2\", the rules \"%3 %4\"; you were asked to choose.")
                           .arg(answer->brand, answer->model, m_rules.brandName, m_rules.model);
    }
    static const QHash<IdentityOutcome::Kind, QString> results{
        {IdentityOutcome::Kind::Refused, QStringLiteral("refused, the rules' answer is used")},
        {IdentityOutcome::Kind::Suggests, QStringLiteral("shown as a suggestion (you were asked)")},
        {IdentityOutcome::Kind::Agrees, QStringLiteral("accepted: agrees with the rules")},
        {IdentityOutcome::Kind::Disagrees, QStringLiteral("asked: you choose between the two")}};
    logLine(QStringLiteral("brand \"%1\", model \"%2\", product \"%3\"").arg(answer->brand, answer->model, answer->product),
            verdict.accepted ? QString() : QStringLiteral("%1: %2").arg(verdict.guard, verdict.why), results.value(outcome.kind));
    emit identityDone(outcome);
}

QString ModelAssist::titleOf(const lookup::Row &row) const
{
    switch (row.source) {
    case lookup::Row::Source::Sold:
        return m_result.sold.value(row.index).title;
    case lookup::Row::Source::Active:
        return m_result.active.value(row.index).title;
    case lookup::Row::Source::Research:
        return m_result.research.value(row.index).title;
    }
    return {};
}

QString ModelAssist::itemIdOf(const lookup::Row &row) const
{
    switch (row.source) {
    case lookup::Row::Source::Sold:
        return m_result.sold.value(row.index).itemId;
    case lookup::Row::Source::Active:
        return m_result.active.value(row.index).itemId;
    case lookup::Row::Source::Research:
        return m_result.research.value(row.index).itemId;
    }
    return {};
}

void ModelAssist::sortRows(const lookup::LookupResult &result, const QList<lookup::Row> &rows)
{
    m_step = Step::Sort;
    m_result = result;
    m_rows = rows;
    m_queue.clear();
    m_asked = 0;
    m_moved = 0;
    m_suggested = 0;
    m_refused = 0;
    m_cached = 0;
    // The counted rows first: they make the numbers.
    for (const lookup::Bucket bucket : {lookup::Bucket::SameItem, lookup::Bucket::ToCheck}) {
        for (qsizetype i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).match.bucket == bucket && !titleOf(m_rows.at(i)).isEmpty())
                m_queue.append(i);
        }
    }
    // G8 needs the listing's own row: without its item number no row is
    // asked about.
    if (m_result.item.itemId.isEmpty())
        m_queue.clear();
    m_total = m_queue.size();
    const qsizetype skipped = std::max<qsizetype>(0, m_total - m_options.maxRows);
    if (skipped > 0)
        m_queue.resize(m_options.maxRows);
    m_sortTimer.start();
    if (m_queue.isEmpty()) {
        const QString why = m_result.item.itemId.isEmpty() ? QStringLiteral("not used: the listing's item number was not read")
                                                           : QString();
        QTimer::singleShot(0, this, [this, why] {
            if (m_step == Step::Sort)
                finishSort(why);
        });
        return;
    }
    sortNext();
}

void ModelAssist::sortNext()
{
    if (m_queue.isEmpty()) {
        finishSort(m_total > m_options.maxRows
                       ? QStringLiteral("at most %1 rows are checked; the other %2 keep the rules' answer")
                             .arg(m_options.maxRows)
                             .arg(m_total - m_options.maxRows)
                       : QString());
        return;
    }
    if (m_sortTimer.elapsed() > m_options.sortBudgetMs) {
        finishSort(QStringLiteral("the time limit (%1 minutes) was reached; %2 rows keep the rules' answer")
                       .arg(m_options.sortBudgetMs / 60000)
                       .arg(m_queue.size()));
        return;
    }
    setProgress(QStringLiteral("Sorting %1 rows with the local model (%2 done)").arg(std::min<qsizetype>(m_total, m_options.maxRows)).arg(m_asked));
    const lookup::Row &row = m_rows.at(m_queue.first());
    ask(sortPrompt(m_info->format, m_result.identity.brandName, m_result.identity.model, titleOf(row)), Grammar::Sort,
        QString::fromLatin1(kSortVersion));
}

void ModelAssist::sortAnswer(const std::optional<QString> &text, const QString &error, bool runnerEnded)
{
    if (m_step != Step::Sort || m_queue.isEmpty())
        return;
    const auto noAnswerLine = [this](qsizetype index, const QString &why) {
        const QString id = itemIdOf(m_rows.at(index));
        qCInfo(lcModel).noquote() << QStringLiteral("%1 row %2 (%3): no answer (%4); no guard refused it; result: unchanged; %5 ms")
                                         .arg(m_version, id.isEmpty() ? QStringLiteral("-") : id,
                                              lookup::rowSourceName(m_rows.at(index).source), why)
                                         .arg(m_callClock.elapsed());
    };
    if (!text && runnerEnded) {
        noAnswerLine(m_queue.first(), error);
        finishSort(QStringLiteral("the local model stopped (%1); %2 rows keep the rules' answer").arg(error).arg(m_queue.size()));
        return;
    }
    const qsizetype index = m_queue.takeFirst();
    ++m_asked;
    if (!text) {
        // This row only: it keeps the rules' answer.
        noAnswerLine(index, error);
        sortNext();
        return;
    }
    lookup::Row &row = m_rows[index];
    const auto logLine = [this, &row](const QString &answer, const QString &guard, const QString &result) {
        const QString id = itemIdOf(row);
        qCInfo(lcModel).noquote() << QStringLiteral("%1 row %2 (%3): %4; %5; result: %6; %7 ms%8")
                                         .arg(m_version, id.isEmpty() ? QStringLiteral("-") : id, lookup::rowSourceName(row.source),
                                              answer, guard.isEmpty() ? QStringLiteral("no guard refused it") : guard, result)
                                         .arg(m_callClock.elapsed())
                                         .arg(m_fromCache ? QStringLiteral(" (saved answer)") : QString());
    };
    // G1: an unreadable answer changes nothing.
    const std::optional<QString> parsed = parseSort(*text);
    if (!parsed)
        logLine(QStringLiteral("answer not readable"), QStringLiteral("G1: not the answer format"), QStringLiteral("unchanged"));
    if (const std::optional<QString> category = parsed) {
        const bool own = !m_result.item.itemId.isEmpty() && itemIdOf(row) == m_result.item.itemId;
        const SortEffect effect = sortEffect(row.match.bucket, own, *category, titleOf(row), m_result.identity, *m_words);
        switch (effect.kind) {
        case SortEffect::Kind::ToCheck:
            row.match.bucket = lookup::Bucket::ToCheck;
            row.included = false;
            row.match.reasons.append(effect.reason);
            ++m_moved;
            break;
        case SortEffect::Kind::Suggest:
            row.match.reasons.append(effect.reason);
            ++m_suggested;
            break;
        case SortEffect::Kind::Refused:
            row.match.reasons.append(effect.reason);
            ++m_refused;
            break;
        case SortEffect::Kind::None:
            break;
        }
        static const QHash<SortEffect::Kind, QString> results{
            {SortEffect::Kind::ToCheck, QStringLiteral("moved to \"to check\" (not counted)")},
            {SortEffect::Kind::Suggest, QStringLiteral("suggestion added to the row")},
            {SortEffect::Kind::Refused, QStringLiteral("refused, the row is unchanged")},
            {SortEffect::Kind::None, QStringLiteral("unchanged (agrees, or nothing to do)")}};
        logLine(QStringLiteral("category \"%1\"").arg(*category),
                effect.kind == SortEffect::Kind::Refused || !effect.guard.isEmpty() ? effect.guard : QString(), results.value(effect.kind));
    }
    sortNext();
}

void ModelAssist::finishSort(const QString &stopNote)
{
    m_step = Step::Idle;
    setProgress(QString());
    QString note = QStringLiteral("Local model (%1): checked %2 of %3 rows in %4 s; %5 moved to \"to check\", %6 suggestions on "
                                  "rows to check")
                       .arg(m_info ? m_info->name : QString())
                       .arg(m_asked)
                       .arg(m_total)
                       .arg(m_sortTimer.elapsed() / 1000)
                       .arg(m_moved)
                       .arg(m_suggested);
    if (m_refused > 0)
        note += QStringLiteral(", %1 answers not used by the guards").arg(m_refused);
    if (!stopNote.isEmpty())
        note += QStringLiteral("; ") + stopNote;
    note += QLatin1Char('.');
    emit sortDone(m_rows, note);
}

void ModelAssist::cancel()
{
    if (m_step == Step::Sort) {
        m_client->stop();
        finishSort(QStringLiteral("stopped by you; %1 rows keep the rules' answer").arg(m_queue.size()));
    } else if (m_step == Step::Identity) {
        m_client->stop();
        identityAnswer(std::nullopt, QStringLiteral("stopped by you"));
    }
}

} // namespace app::model
