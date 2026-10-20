#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::plugins {

// Plugins, API version 1 (DESIGN 5, PLAN 15). A plugin is two files:
// manifest.json (below) and main.js (the script, run by PluginHost in a
// JavaScript engine of its own with no file, network or process access).
//
// manifest.json, every field required unless marked optional:
//   id                1-64 characters: a-z, 0-9, "-" (Settings::isPluginId)
//   name              1-80 characters
//   version           1-32 characters
//   author            1-80 characters
//   description       optional, up to 500 characters
//   pluginApiVersion  1
//   type              "matcher", "valuator", "flagger" or "exporter"
//   permissions       the data the plugin is given, a list of:
//                       "listing"   the pasted listing
//                       "identity"  its brand, model, product name, category
//                       "rows"      the comparison rows
//                       "export"    save a file in the Downloads folder
//                                   through the app (exporters only, and
//                                   every exporter needs it)
//                     matchers and valuators need "rows".
// Anything else in the file is refused, so a later field is never silently
// ignored by this version.
enum class PluginType { Matcher, Valuator, Flagger, Exporter };

struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    PluginType type = PluginType::Flagger;
    QStringList permissions;

    // The script function main.js must define for this type: "match",
    // "value", "flags" or "exportLookup".
    QString function() const;
    QString typeName() const; // "matcher", ...
    bool allows(const QString &permission) const { return permissions.contains(permission); }

    // std::nullopt (and *error, in plain words) when `json` is not a valid
    // version-1 manifest.
    static std::optional<PluginManifest> fromJson(const QByteArray &json, QString *error);
};

// The plugin API version this app runs.
constexpr int kPluginApiVersion = 1;

// Largest manifest.json and main.js the app reads.
constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr qint64 kMaxScriptBytes = 1024 * 1024;

// A permission in plain words, for the screen that shows it before the user
// enables the plugin.
QString permissionText(const QString &permission);

} // namespace app::plugins
