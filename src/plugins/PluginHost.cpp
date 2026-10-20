#include "plugins/PluginHost.h"

#include "lookup/LookupStore.h"

#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace app::plugins {

namespace {

constexpr qint64 kMaxCents = 100'000'000'000;

// Stops the engine's script after `ms` milliseconds unless stop() comes
// first. QJSEngine::setInterrupted is thread safe (Qt docs); the watchdog
// thread calls nothing else.
class Watchdog {
public:
    Watchdog(QJSEngine *engine, int ms)
        : m_thread([this, engine, ms]() {
              std::unique_lock<std::mutex> lock(m_mutex);
              if (!m_wake.wait_for(lock, std::chrono::milliseconds(ms), [this]() { return m_stopped; })) {
                  m_fired = true;
                  engine->setInterrupted(true);
              }
          })
    {
    }
    ~Watchdog() { stop(); }
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;

    // Ends the watch; true when the time limit was reached.
    bool stop()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        return m_fired;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopped = false;
    bool m_fired = false;
    std::thread m_thread;
};

QJsonValue optionalNumber(const std::optional<qint64> &value)
{
    return value ? QJsonValue(static_cast<double>(*value)) : QJsonValue(QJsonValue::Null);
}

QJsonObject listingJson(const lookup::ItemRecord &item)
{
    QJsonArray specifics;
    for (const lookup::ItemRecord::Specific &specific : item.specifics)
        specifics.append(QJsonObject{{QStringLiteral("label"), specific.label}, {QStringLiteral("value"), specific.value}});
    return QJsonObject{
        {QStringLiteral("itemId"), item.itemId},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("priceCents"), optionalNumber(item.priceCents)},
        {QStringLiteral("shippingCents"), optionalNumber(item.shippingCents)},
        {QStringLiteral("totalCents"), optionalNumber(item.totalCents())},
        {QStringLiteral("currency"), item.currency},
        {QStringLiteral("condition"), item.condition},
        {QStringLiteral("sellerName"), item.sellerName},
        {QStringLiteral("sellerFeedbackCount"),
         item.sellerFeedbackCount ? QJsonValue(*item.sellerFeedbackCount) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("sellerPositivePercent"),
         item.sellerPositivePercent ? QJsonValue(*item.sellerPositivePercent) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("auction"), item.auction},
        {QStringLiteral("bids"), item.bids ? QJsonValue(*item.bids) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("buyItNow"), item.buyItNow},
        {QStringLiteral("bestOffer"), item.bestOffer},
        {QStringLiteral("specifics"), specifics},
    };
}

// A string of 1..maxLength characters (0..maxLength when optional) at
// `key`; std::nullopt otherwise.
std::optional<QString> textOf(const QJsonObject &object, const char *key, qsizetype maxLength, bool optional = false)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (optional && (value.isUndefined() || value.isNull()))
        return QString();
    if (!value.isString())
        return std::nullopt;
    const QString text = value.toString().simplified();
    if ((text.isEmpty() && !optional) || text.size() > maxLength)
        return std::nullopt;
    return text;
}

} // namespace

struct PluginHost::Loaded {
    PluginInfo info;
    std::unique_ptr<QJSEngine> engine;
    // Taken before main.js runs, so the script cannot replace them.
    QJSValue parse;
    QJSValue stringify;
    QJSValue function;
};

PluginHost::PluginHost(const PluginCatalog &catalog, std::function<int()> timeLimitMs)
    : m_catalog(catalog), m_timeLimitMs(std::move(timeLimitMs))
{
}

PluginHost::~PluginHost() = default;

void PluginHost::fail(const QString &id, const QString &reason)
{
    const auto it = std::find_if(m_loaded.begin(), m_loaded.end(), [&id](const auto &loaded) { return loaded->info.manifest.id == id; });
    QString name = id;
    if (it != m_loaded.end()) {
        name = (*it)->info.manifest.name;
        m_loaded.erase(it);
    }
    m_catalog.disableAfterFailure(id, reason);
    m_failures.append(QStringLiteral("%1 was disabled: %2").arg(name, reason));
}

void PluginHost::reload()
{
    m_loaded.clear();
    for (const PluginInfo &info : m_catalog.plugins()) {
        if (!info.enabled)
            continue;
        QString error;
        const std::optional<QString> script = m_catalog.script(info, &error);
        if (!script) {
            fail(info.manifest.id, error);
            continue;
        }
        auto loaded = std::make_unique<Loaded>();
        loaded->info = info;
        // No installExtensions(): no console, no qsTr, no gc().
        loaded->engine = std::make_unique<QJSEngine>();
        const QJSValue json = loaded->engine->globalObject().property(QStringLiteral("JSON"));
        loaded->parse = json.property(QStringLiteral("parse"));
        loaded->stringify = json.property(QStringLiteral("stringify"));
        const int limit = m_timeLimitMs();
        Watchdog watchdog(loaded->engine.get(), limit);
        const QJSValue result = loaded->engine->evaluate(*script, info.manifest.id + QStringLiteral("/main.js"));
        const bool late = watchdog.stop();
        loaded->engine->setInterrupted(false);
        if (late || result.isError() || loaded->engine->hasError()) {
            const QJSValue thrown = loaded->engine->hasError() ? loaded->engine->catchError() : result;
            fail(info.manifest.id, late ? QStringLiteral("main.js did not finish loading within %1 ms").arg(limit)
                                        : QStringLiteral("main.js does not load: %1").arg(thrown.toString()));
            continue;
        }
        loaded->function = loaded->engine->globalObject().property(info.manifest.function());
        if (!loaded->function.isCallable()) {
            fail(info.manifest.id, QStringLiteral("main.js defines no function %1()").arg(info.manifest.function()));
            continue;
        }
        m_loaded.push_back(std::move(loaded));
    }
}

QJsonObject PluginHost::inputFor(const PluginManifest &manifest, const lookup::LookupResult &result,
                                 const QList<lookup::Row> &rows)
{
    QJsonObject input{{QStringLiteral("apiVersion"), kPluginApiVersion}};
    if (manifest.allows(QStringLiteral("listing")))
        input.insert(QStringLiteral("listing"), listingJson(result.item));
    if (manifest.allows(QStringLiteral("identity"))) {
        input.insert(QStringLiteral("identity"), QJsonObject{{QStringLiteral("brand"), result.identity.brandName},
                                                             {QStringLiteral("model"), result.identity.model},
                                                             {QStringLiteral("productName"), result.identity.productName},
                                                             {QStringLiteral("category"), result.identity.category}});
    }
    if (manifest.allows(QStringLiteral("rows"))) {
        QJsonArray list;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            const lookup::Row &row = rows.at(i);
            list.append(QJsonObject{
                {QStringLiteral("row"), static_cast<int>(i)},
                {QStringLiteral("source"), lookup::rowSourceName(row.source)},
                {QStringLiteral("itemId"), lookup::rowItemId(result, row)},
                {QStringLiteral("title"), lookup::rowTitle(result, row)},
                {QStringLiteral("totalCents"), optionalNumber(lookup::rowTotal(result, row))},
                {QStringLiteral("condition"), lookup::rowCondition(result, row)},
                {QStringLiteral("date"), lookup::rowDate(result, row)},
                {QStringLiteral("sellerName"), lookup::rowSeller(result, row)},
                {QStringLiteral("bucket"), lookup::LookupStore::bucketId(row.match.bucket)},
                {QStringLiteral("included"), row.included},
            });
        }
        input.insert(QStringLiteral("rows"), list);
    }
    return input;
}

std::optional<QJsonValue> PluginHost::call(Loaded &plugin, const QJsonObject &input, QString *error)
{
    const QByteArray json = QJsonDocument(input).toJson(QJsonDocument::Compact);
    if (json.size() > kMaxInputBytes) {
        *error = QStringLiteral("the lookup's data is larger than %1 MB").arg(kMaxInputBytes / (1024 * 1024));
        return std::nullopt;
    }
    QJSEngine &engine = *plugin.engine;
    const int limit = m_timeLimitMs();
    Watchdog watchdog(&engine, limit);
    const QJSValue argument = plugin.parse.call({QString::fromUtf8(json)});
    const QJSValue answer = plugin.function.call({argument});
    const bool empty = answer.isUndefined() || answer.isNull();
    const QJSValue text = empty || answer.isError() ? QJSValue() : plugin.stringify.call({answer});
    const bool late = watchdog.stop();
    engine.setInterrupted(false);
    if (late) {
        *error = QStringLiteral("it did not finish within %1 ms").arg(limit);
        return std::nullopt;
    }
    if (engine.hasError() || answer.isError() || text.isError()) {
        const QJSValue thrown = engine.hasError() ? engine.catchError() : (answer.isError() ? answer : text);
        *error = QStringLiteral("it stopped with an error: %1").arg(thrown.toString());
        return std::nullopt;
    }
    if (empty)
        return QJsonValue(QJsonValue::Null);
    const QString out = text.toString();
    if (!text.isString() || out.size() > kMaxOutputChars) {
        *error = QStringLiteral("its answer is not plain data or larger than %1 KB").arg(kMaxOutputChars / 1024);
        return std::nullopt;
    }
    // Wrapped in an array so any JSON value parses.
    const QJsonDocument document = QJsonDocument::fromJson(QStringLiteral("[%1]").arg(out).toUtf8());
    if (!document.isArray() || document.array().size() != 1) {
        *error = QStringLiteral("its answer is not plain data");
        return std::nullopt;
    }
    return document.array().first();
}

void PluginHost::runAll(PluginType type, const lookup::LookupResult &result, const QList<lookup::Row> &rows,
                        const std::function<bool(const Loaded &, const QJsonValue &, QString *)> &take)
{
    // By id; a failing plugin leaves m_loaded, so walk a list of ids.
    QStringList ids;
    for (const auto &loaded : m_loaded) {
        if (loaded->info.manifest.type == type)
            ids.append(loaded->info.manifest.id);
    }
    ids.sort();
    for (const QString &id : std::as_const(ids)) {
        const auto it = std::find_if(m_loaded.begin(), m_loaded.end(), [&id](const auto &loaded) { return loaded->info.manifest.id == id; });
        if (it == m_loaded.end())
            continue;
        Loaded &plugin = **it;
        QString error;
        const std::optional<QJsonValue> answer = call(plugin, inputFor(plugin.info.manifest, result, rows), &error);
        if (!answer || !take(plugin, *answer, &error))
            fail(id, error);
    }
}

void PluginHost::applyMatchers(const lookup::LookupResult &result, QList<lookup::Row> *rows)
{
    runAll(PluginType::Matcher, result, *rows, [rows](const Loaded &plugin, const QJsonValue &answer, QString *error) {
        if (answer.isNull())
            return true;
        if (!answer.isArray() || answer.toArray().size() > rows->size()) {
            *error = QStringLiteral("match() must return null or a list of at most one change per row");
            return false;
        }
        QList<QPair<qsizetype, lookup::Match>> changes;
        for (const QJsonValue &value : answer.toArray()) {
            const QJsonObject change = value.toObject();
            const QJsonValue row = change.value(QStringLiteral("row"));
            const std::optional<lookup::Bucket> bucket =
                lookup::LookupStore::bucketFromId(change.value(QStringLiteral("bucket")).toString());
            const std::optional<QString> reason = textOf(change, "reason", 200);
            const double index = row.toDouble(-1);
            if (!value.isObject() || !row.isDouble() || index != static_cast<double>(static_cast<qsizetype>(index)) || index < 0
                || index >= static_cast<double>(rows->size()) || !bucket || !reason) {
                *error = QStringLiteral("match() returned a change that is not {row, bucket, reason} for a row it was given");
                return false;
            }
            lookup::Match match = rows->at(static_cast<qsizetype>(index)).match;
            match.bucket = *bucket;
            match.reasons.append(QStringLiteral("%1: %2").arg(plugin.info.manifest.name, *reason));
            changes.append({static_cast<qsizetype>(index), match});
        }
        // Only a whole valid answer changes anything.
        for (const auto &[index, match] : std::as_const(changes)) {
            (*rows)[index].match = match;
            (*rows)[index].included = match.bucket == lookup::Bucket::SameItem;
        }
        return true;
    });
}

QList<PluginFlag> PluginHost::flags(const lookup::LookupResult &result, const QList<lookup::Row> &rows)
{
    QList<PluginFlag> list;
    runAll(PluginType::Flagger, result, rows, [&list](const Loaded &plugin, const QJsonValue &answer, QString *error) {
        if (answer.isNull())
            return true;
        if (!answer.isArray() || answer.toArray().size() > kMaxFlags) {
            *error = QStringLiteral("flags() must return null or a list of at most %1 flags").arg(kMaxFlags);
            return false;
        }
        QList<PluginFlag> own;
        for (const QJsonValue &value : answer.toArray()) {
            const QJsonObject flag = value.toObject();
            const std::optional<QString> text = textOf(flag, "text", 120);
            const QString kind = flag.value(QStringLiteral("kind")).toString();
            if (!value.isObject() || !text || (kind != QLatin1String("warn") && kind != QLatin1String("info"))) {
                *error = QStringLiteral("flags() returned a flag that is not {text, kind: \"warn\" or \"info\"}");
                return false;
            }
            own.append({plugin.info.manifest.name, *text, kind});
        }
        list.append(own);
        return true;
    });
    return list;
}

QList<PluginValue> PluginHost::values(const lookup::LookupResult &result, const QList<lookup::Row> &rows)
{
    QList<PluginValue> list;
    runAll(PluginType::Valuator, result, rows, [&list](const Loaded &plugin, const QJsonValue &answer, QString *error) {
        if (answer.isNull())
            return true;
        const QJsonObject value = answer.toObject();
        const std::optional<QString> label = textOf(value, "label", 60);
        const std::optional<QString> note = textOf(value, "note", 200, true);
        const QJsonValue cents = value.value(QStringLiteral("cents"));
        const double amount = cents.toDouble(-1);
        if (!answer.isObject() || !label || !note || !cents.isDouble() || amount < 0 || amount > static_cast<double>(kMaxCents)
            || amount != static_cast<double>(static_cast<qint64>(amount))) {
            *error = QStringLiteral("value() must return null or {label, cents (a whole number of cents), note}");
            return false;
        }
        list.append({plugin.info.manifest.name, *label, static_cast<qint64>(amount), *note});
        return true;
    });
    return list;
}

QList<PluginManifest> PluginHost::exporters() const
{
    QList<PluginManifest> list;
    for (const auto &loaded : m_loaded) {
        if (loaded->info.manifest.type == PluginType::Exporter)
            list.append(loaded->info.manifest);
    }
    return list;
}

std::optional<PluginExport> PluginHost::exportLookup(const QString &id, const lookup::LookupResult &result,
                                                     const QList<lookup::Row> &rows, QString *error)
{
    const auto it = std::find_if(m_loaded.begin(), m_loaded.end(), [&id](const auto &loaded) {
        return loaded->info.manifest.id == id && loaded->info.manifest.type == PluginType::Exporter;
    });
    if (it == m_loaded.end()) {
        *error = QStringLiteral("there is no enabled exporter %1").arg(id);
        return std::nullopt;
    }
    Loaded &plugin = **it;
    const QString name = plugin.info.manifest.name;
    QString why;
    const std::optional<QJsonValue> answer = call(plugin, inputFor(plugin.info.manifest, result, rows), &why);
    if (answer) {
        static const QRegularExpression fileName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._-]{0,95}\\.(csv|md|txt|json)$"));
        const QJsonObject object = answer->toObject();
        const QString file = object.value(QStringLiteral("fileName")).toString();
        const QJsonValue text = object.value(QStringLiteral("text"));
        const QByteArray data = text.toString().toUtf8();
        if (answer->isObject() && fileName.match(file).hasMatch() && !file.contains(QStringLiteral("..")) && text.isString()
            && data.size() <= kMaxExportBytes)
            return PluginExport{file, data};
        why = QStringLiteral("exportLookup() must return {fileName (ending .csv, .md, .txt or .json), text of at most %1 MB}")
                  .arg(kMaxExportBytes / (1024 * 1024));
    }
    fail(id, why);
    *error = QStringLiteral("%1 was disabled: %2").arg(name, why);
    return std::nullopt;
}

QStringList PluginHost::takeFailures()
{
    return std::exchange(m_failures, {});
}

} // namespace app::plugins
