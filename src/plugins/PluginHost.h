#pragma once

#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginRunner.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace app::plugins {

// A warning or note from a flagger, shown with the plugin's name.
struct PluginFlag {
    QString pluginName;
    QString text;
    QString kind; // "warn" or "info"
};

// A figure from a valuator, shown next to the app's own numbers (never in
// their place).
struct PluginValue {
    QString pluginName;
    QString label;
    qint64 cents = 0;
    QString note;
};

// A file an exporter made; the app saves it (core/ExportFiles).
struct PluginExport {
    QString fileName;
    QByteArray data;
};

// Runs the enabled plugins, API version 1 (DESIGN 5, PLAN 15).
//
// Sandbox: plugin code never runs inside the app. Each check and each call
// runs in a runner process of its own (PluginRunner.h): a QJSEngine created
// without any of its extensions (no console, no qsTr, no gc()), with no C++
// object passed in, so the script has the ECMAScript built-ins and nothing
// else - no file, network or process access. It sees only the data its
// manifest's permissions grant, as plain JSON (inputFor), and returns plain
// JSON.
//
// Limits: every check and call is stopped after the time limit (Settings
// "plugins/callTimeLimitMs"): inside the runner by QJSEngine::setInterrupted,
// and by the app, which kills a runner still running after the limit (plus
// the time a process needs to start). QJSEngine offers no memory limit: a
// script that uses up memory ends its own runner process, never the app.
// Promise callbacks never run (the runner ends after the answer). What goes
// in is capped (kMaxInputBytes) and what comes out is checked against the
// shapes below and capped (kMaxOutputChars, the exporter's file size).
//
// Failure: a script error, a call over the time limit or an answer of the
// wrong shape disables the plugin (PluginCatalog::disableAfterFailure) and
// is reported (takeFailures); the lookup goes on without it.
//
// The functions main.js defines, each called with one argument, the input
// object {apiVersion: 1, listing?, identity?, rows?} (see inputFor):
//   matcher   match(input)        -> null or [{row, bucket, reason}]: move
//                                    rows[row] to `bucket` (same_item,
//                                    documentation, part_fits, companion,
//                                    accessory, lot, unknown_item, to_check,
//                                    not_this_item), `reason` shown with it
//   valuator  value(input)        -> null or {label, cents, note?}
//   flagger   flags(input)        -> null or [{text, kind: "warn"|"info"}],
//                                    at most kMaxFlags
//   exporter  exportLookup(input) -> {fileName, text}: a plain file name
//                                    ending .csv, .md, .txt or .json
// Texts: label up to 60 characters, reason and note up to 200, text up to
// 120; cents a whole number from 0 to 100,000,000,000 (one billion dollars).
class PluginHost {
public:
    static constexpr qsizetype kMaxInputBytes = 8 * 1024 * 1024;
    static constexpr qsizetype kMaxOutputChars = 256 * 1024;
    static constexpr qsizetype kMaxExportBytes = 16 * 1024 * 1024;
    static constexpr qsizetype kMaxFlags = 5;

    // `runner`: how to start the runner process. `timeLimitMs` gives the time
    // limit per call when a call starts.
    PluginHost(const PluginCatalog &catalog, RunnerCommand runner, std::function<int()> timeLimitMs);
    ~PluginHost();
    PluginHost(const PluginHost &) = delete;
    PluginHost &operator=(const PluginHost &) = delete;

    // Loads every enabled plugin (and drops the others). A plugin whose
    // main.js fails to load is disabled and reported.
    void reload();

    // The matchers, in id order, each on the rows as the ones before left
    // them. A moved row is counted only when it moves to same_item.
    void applyMatchers(const lookup::LookupResult &result, QList<lookup::Row> *rows);
    QList<PluginFlag> flags(const lookup::LookupResult &result, const QList<lookup::Row> &rows);
    QList<PluginValue> values(const lookup::LookupResult &result, const QList<lookup::Row> &rows);
    // The loaded exporters.
    QList<PluginManifest> exporters() const;
    // Runs exporter `id`; std::nullopt (and *error) when it cannot.
    std::optional<PluginExport> exportLookup(const QString &id, const lookup::LookupResult &result,
                                             const QList<lookup::Row> &rows, QString *error);

    // "<name> was disabled: <why>" for every plugin disabled since the last
    // call, oldest first.
    QStringList takeFailures();

    // The input object a plugin with `manifest` gets for this lookup.
    static QJsonObject inputFor(const PluginManifest &manifest, const lookup::LookupResult &result,
                                const QList<lookup::Row> &rows);

private:
    struct Loaded;

    // Calls the plugin's function with `input`: its answer as JSON (null for
    // null or undefined); std::nullopt (and *error) when it failed.
    std::optional<QJsonValue> call(Loaded &plugin, const QJsonObject &input, QString *error);
    // Calls each plugin of `type` (by id) and hands its answer to `take`,
    // which returns false (and *error) when the answer has the wrong shape;
    // a failed call or a wrong answer disables the plugin.
    void runAll(PluginType type, const lookup::LookupResult &result, const QList<lookup::Row> &rows,
                const std::function<bool(const Loaded &, const QJsonValue &, QString *)> &take);
    void fail(const QString &id, const QString &reason);

    const PluginCatalog &m_catalog;
    RunnerCommand m_runner;
    std::function<int()> m_timeLimitMs;
    std::vector<std::unique_ptr<Loaded>> m_loaded;
    QStringList m_failures;
};

} // namespace app::plugins
