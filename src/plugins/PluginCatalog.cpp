#include "plugins/PluginCatalog.h"

#include "core/PathChecks.h"
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
// Qt's own ZIP reader. It is private Qt API (no compatibility promise
// between Qt versions); the app builds against one pinned Qt version
// (PLAN 1), and tst_plugins covers it.
#include <QtCore/private/qzipreader_p.h>

#include <algorithm>
#include <tuple>
#include <utility>

namespace app::plugins {

namespace {

const QString kManifestName = QStringLiteral("manifest.json");
const QString kScriptName = QStringLiteral("main.js");
const QString kBuiltInRoot = QStringLiteral(":/plugins");

struct PluginFiles {
    QByteArray manifest;
    QByteArray script;
};

QString shown(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

// The file at `path`: a plain file (no link; resources are plain) of at
// most `maxBytes`. std::nullopt (and *error) otherwise.
std::optional<QByteArray> readSmallFile(const QString &path, qint64 maxBytes, QString *error)
{
    if (!path.startsWith(QStringLiteral(":/")) && !isPlainFile(path)) {
        *error = QStringLiteral("%1 is missing or is a link").arg(shown(path));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 cannot be read").arg(shown(path));
        return std::nullopt;
    }
    const QByteArray data = file.read(maxBytes + 1);
    if (data.size() > maxBytes) {
        *error = QStringLiteral("%1 is larger than %2 KB").arg(shown(path)).arg(maxBytes / 1024);
        return std::nullopt;
    }
    return data;
}

// Writes `data` to `path` in one step (QSaveFile: a temporary file, then a
// rename); never through a link.
bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    if (isLink(path)) {
        *error = QStringLiteral("%1 is a link; links are never followed").arg(shown(path));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1 cannot be written: %2").arg(shown(path), file.errorString());
        return false;
    }
    return true;
}

std::optional<PluginFiles> readFolder(const QString &folder, QString *error)
{
    const std::optional<QByteArray> manifest = readSmallFile(folder + QLatin1Char('/') + kManifestName, kMaxManifestBytes, error);
    if (!manifest)
        return std::nullopt;
    const std::optional<QByteArray> script = readSmallFile(folder + QLatin1Char('/') + kScriptName, kMaxScriptBytes, error);
    if (!script)
        return std::nullopt;
    return PluginFiles{*manifest, *script};
}

// manifest.json and main.js from a .zip: at its top or inside one folder.
// Nothing else in it is read, and nothing is extracted to disk.
std::optional<PluginFiles> readZip(const QString &path, QString *error)
{
    if (!isPlainFile(path) || QFileInfo(path).size() > PluginCatalog::kMaxZipBytes) {
        *error = QStringLiteral("%1 is missing, a link, or larger than %2 MB")
                     .arg(shown(path))
                     .arg(PluginCatalog::kMaxZipBytes / (1024 * 1024));
        return std::nullopt;
    }
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        *error = QStringLiteral("%1 is not a readable .zip file").arg(shown(path));
        return std::nullopt;
    }
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    // Where manifest.json is: "" (the top) or "<folder>/".
    QStringList prefixes;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.filePath == kManifestName)
            prefixes.append(QString());
        else if (entry.filePath.endsWith(QLatin1Char('/') + kManifestName) && entry.filePath.count(QLatin1Char('/')) == 1)
            prefixes.append(entry.filePath.chopped(kManifestName.size()));
    }
    if (prefixes.size() != 1) {
        *error = QStringLiteral("the .zip must hold one manifest.json, at its top or inside one folder");
        return std::nullopt;
    }
    const QString prefix = prefixes.first();
    PluginFiles files;
    for (const auto &[name, limit, target] : {std::tuple{kManifestName, kMaxManifestBytes, &files.manifest},
                                              std::tuple{kScriptName, kMaxScriptBytes, &files.script}}) {
        const auto entry = std::find_if(entries.cbegin(), entries.cend(),
                                        [&](const QZipReader::FileInfo &info) { return info.filePath == prefix + name; });
        if (entry == entries.cend() || !entry->isFile || entry->isSymLink) {
            *error = QStringLiteral("%1 is missing from the .zip or is not a plain file").arg(prefix + name);
            return std::nullopt;
        }
        if (entry->size > limit) {
            *error = QStringLiteral("%1 in the .zip is larger than %2 KB").arg(prefix + name).arg(limit / 1024);
            return std::nullopt;
        }
        *target = zip.fileData(prefix + name);
        if (zip.status() != QZipReader::NoError || target->size() > limit) {
            *error = QStringLiteral("%1 cannot be read from the .zip").arg(prefix + name);
            return std::nullopt;
        }
    }
    return files;
}

} // namespace

PluginCatalog::PluginCatalog(WorkFolder folder) : m_folder(std::move(folder)) {}

QStringList PluginCatalog::builtInIds() const
{
    return QDir(kBuiltInRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

std::optional<PluginInfo> PluginCatalog::readInstalled(const QString &folder, QString *problem) const
{
    if (isLink(folder)) {
        *problem = QStringLiteral("%1 is a link; it is not used").arg(shown(folder));
        return std::nullopt;
    }
    QString error;
    const std::optional<QByteArray> manifestData = readSmallFile(folder + QLatin1Char('/') + kManifestName, kMaxManifestBytes, &error);
    std::optional<PluginManifest> manifest = manifestData ? PluginManifest::fromJson(*manifestData, &error) : std::nullopt;
    if (!manifest) {
        *problem = QStringLiteral("%1: %2").arg(shown(folder), error);
        return std::nullopt;
    }
    if (manifest->id != QFileInfo(folder).fileName() || builtInIds().contains(manifest->id)) {
        *problem = QStringLiteral("%1: the folder's name must be the plugin's id, and no built-in example's id").arg(shown(folder));
        return std::nullopt;
    }
    PluginInfo info;
    info.manifest = *manifest;
    info.location = folder;
    return info;
}

QList<PluginInfo> PluginCatalog::plugins(QStringList *problems) const
{
    QList<PluginInfo> list;
    for (const QString &id : builtInIds()) {
        QString error;
        const QString location = kBuiltInRoot + QLatin1Char('/') + id;
        const std::optional<QByteArray> data = readSmallFile(location + QLatin1Char('/') + kManifestName, kMaxManifestBytes, &error);
        const std::optional<PluginManifest> manifest = data ? PluginManifest::fromJson(*data, &error) : std::nullopt;
        if (!manifest || manifest->id != id) {
            if (problems)
                problems->append(QStringLiteral("built-in example %1: %2").arg(id, manifest ? QStringLiteral("wrong id") : error));
            continue;
        }
        PluginInfo info;
        info.manifest = *manifest;
        info.builtIn = true;
        info.location = location;
        list.append(info);
    }
    const QString root = m_folder.pluginsDir();
    if (!isLink(root)) {
        for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QString problem;
            if (const std::optional<PluginInfo> info = readInstalled(root + QLatin1Char('/') + name, &problem))
                list.append(*info);
            else if (problems)
                problems->append(problem);
        }
    } else if (problems) {
        problems->append(QStringLiteral("%1 is a link; installed plugins are not used").arg(shown(root)));
    }
    const Settings settings(m_folder.settingsPath());
    const QStringList enabled = settings.enabledPlugins();
    for (PluginInfo &info : list) {
        info.enabled = enabled.contains(info.manifest.id);
        info.failure = settings.pluginFailure(info.manifest.id);
    }
    return list;
}

std::optional<PluginInfo> PluginCatalog::find(const QString &id) const
{
    for (const PluginInfo &info : plugins()) {
        if (info.manifest.id == id)
            return info;
    }
    return std::nullopt;
}

std::optional<QString> PluginCatalog::script(const PluginInfo &plugin, QString *error) const
{
    const std::optional<QByteArray> data = readSmallFile(plugin.location + QLatin1Char('/') + kScriptName, kMaxScriptBytes, error);
    if (!data)
        return std::nullopt;
    return QString::fromUtf8(*data);
}

bool PluginCatalog::setEnabled(const QString &id, bool enabled, QString *error) const
{
    if (!find(id)) {
        *error = QStringLiteral("there is no plugin %1").arg(id);
        return false;
    }
    Settings settings(m_folder.settingsPath());
    settings.setPluginEnabled(id, enabled);
    if (enabled)
        settings.setPluginFailure(id, QString());
    if (!settings.sync()) {
        *error = QStringLiteral("the settings file cannot be written");
        return false;
    }
    return true;
}

bool PluginCatalog::disableAfterFailure(const QString &id, const QString &reason) const
{
    Settings settings(m_folder.settingsPath());
    return settings.setPluginEnabled(id, false) && settings.setPluginFailure(id, reason) && settings.sync();
}

std::optional<PluginManifest> PluginCatalog::install(const QString &path, QString *error) const
{
    const QFileInfo source(path);
    std::optional<PluginFiles> files;
    if (path.isEmpty() || isLink(path)) {
        *error = QStringLiteral("choose a plugin folder or a .zip file (links are not followed)");
        return std::nullopt;
    }
    if (source.isDir())
        files = readFolder(path, error);
    else if (source.isFile() && path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
        files = readZip(path, error);
    else
        *error = QStringLiteral("%1 is neither a folder nor a .zip file").arg(shown(path));
    if (!files)
        return std::nullopt;
    const std::optional<PluginManifest> manifest = PluginManifest::fromJson(files->manifest, error);
    if (!manifest)
        return std::nullopt;
    if (builtInIds().contains(manifest->id)) {
        *error = QStringLiteral("\"%1\" is the id of a built-in example; a plugin needs an id of its own").arg(manifest->id);
        return std::nullopt;
    }
    const QString root = m_folder.pluginsDir();
    const QString target = root + QLatin1Char('/') + manifest->id;
    if (isLink(root) || !QFileInfo(root).isDir() || isLink(target) || (somethingAt(target) && !QFileInfo(target).isDir())) {
        *error = QStringLiteral("%1 cannot be used: it is missing, a link or not a folder").arg(shown(isLink(root) ? root : target));
        return std::nullopt;
    }
    if (!QDir().mkpath(target)) {
        *error = QStringLiteral("%1 cannot be created").arg(shown(target));
        return std::nullopt;
    }
    // Disabled before the files change: a replaced plugin starts disabled.
    Settings settings(m_folder.settingsPath());
    settings.setPluginEnabled(manifest->id, false);
    settings.setPluginFailure(manifest->id, QString());
    if (!settings.sync()) {
        *error = QStringLiteral("the settings file cannot be written");
        return std::nullopt;
    }
    if (!writeFile(target + QLatin1Char('/') + kManifestName, files->manifest, error)
        || !writeFile(target + QLatin1Char('/') + kScriptName, files->script, error))
        return std::nullopt;
    return manifest;
}

bool PluginCatalog::remove(const QString &id, QString *error) const
{
    const std::optional<PluginInfo> info = find(id);
    if (!info || info->builtIn) {
        *error = info ? QStringLiteral("built-in examples cannot be removed; disable it instead")
                      : QStringLiteral("there is no installed plugin %1").arg(id);
        return false;
    }
    Settings settings(m_folder.settingsPath());
    settings.setPluginEnabled(id, false);
    settings.setPluginFailure(id, QString());
    if (!settings.sync()) {
        *error = QStringLiteral("the settings file cannot be written");
        return false;
    }
    const QString folder = m_folder.pluginsDir() + QLatin1Char('/') + id;
    for (const QString &name : {kManifestName, kScriptName}) {
        const QString file = folder + QLatin1Char('/') + name;
        if (somethingAt(file) && (!isPlainFile(file) || !QFile::remove(file))) {
            *error = QStringLiteral("%1 cannot be removed").arg(shown(file));
            return false;
        }
    }
    if (!QDir(m_folder.pluginsDir()).rmdir(id)) {
        *error = QStringLiteral("the plugin is removed, but %1 holds other files and was left as it is").arg(shown(folder));
        return false;
    }
    return true;
}

} // namespace app::plugins
