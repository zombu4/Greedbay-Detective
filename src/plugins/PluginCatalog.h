#pragma once

#include "core/WorkFolder.h"
#include "plugins/PluginManifest.h"
#include "plugins/PluginRunner.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::plugins {

// A plugin the app knows: a built-in example (shipped inside the app as
// :/plugins/<id>/manifest.json and main.js; read-only) or one the user
// installed (<work>/plugins/<id>/manifest.json and main.js).
struct PluginInfo {
    PluginManifest manifest;
    bool builtIn = false;
    QString location; // ":/plugins/<id>" or the plugin's folder
    bool enabled = false;
    QString failure; // why the app disabled it after a failure; empty when it did not
};

// The plugins and their state. Enabled ids and failures live in the work
// folder's settings.ini (core/Settings); every plugin is disabled until the
// user enables it, also right after it is installed or replaced.
class PluginCatalog {
public:
    // `runner`: how to start the runner process that reads .zip files
    // (PluginRunner.h).
    PluginCatalog(WorkFolder folder, RunnerCommand runner);

    // The built-in examples, then the installed plugins, each by id.
    // *problems (may be null): installed folders that cannot be used, and why.
    QList<PluginInfo> plugins(QStringList *problems = nullptr) const;
    std::optional<PluginInfo> find(const QString &id) const;
    // The plugin's main.js; std::nullopt (and *error) when it cannot be read.
    std::optional<QString> script(const PluginInfo &plugin, QString *error) const;

    // Enabling clears a recorded failure. False (and *error) for an unknown
    // plugin or when settings.ini cannot be written.
    bool setEnabled(const QString &id, bool enabled, QString *error) const;
    // Disables the plugin after a failure and records why (shown on the
    // Plugins screen until the user enables it again).
    bool disableAfterFailure(const QString &id, const QString &reason) const;

    // Installs a plugin from `path`: a folder holding manifest.json and
    // main.js, or a .zip file holding them at its top or inside one folder
    // (read by the runner process; nothing else in it is read, and only these
    // two files are written). The manifest must be valid, its id must not be
    // a built-in example's, and neither file may be a link or larger than the
    // limits of PluginManifest.h. An installed plugin of the same id is
    // replaced. The plugin is disabled afterwards. Returns its manifest;
    // std::nullopt (and *error) when nothing was installed.
    std::optional<PluginManifest> install(const QString &path, QString *error) const;

    // Removes an installed plugin: its manifest.json and main.js, then its
    // folder (when the user put other files there, the folder stays and
    // *error says so). Built-in examples cannot be removed.
    bool remove(const QString &id, QString *error) const;

    // Largest .zip file install() reads, and how long its runner may take.
    static constexpr qint64 kMaxZipBytes = 8 * 1024 * 1024;
    static constexpr int kZipTimeLimitMs = 10000;

private:
    std::optional<PluginInfo> readInstalled(const QString &folder, QString *problem) const;
    QStringList builtInIds() const;

    WorkFolder m_folder;
    RunnerCommand m_runner;
};

} // namespace app::plugins
