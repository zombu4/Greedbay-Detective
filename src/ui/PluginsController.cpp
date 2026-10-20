#include "ui/PluginsController.h"

#include "core/Settings.h"

#include <QDir>
#include <QUrl>
#include <QVariantMap>

#include <utility>

namespace app::ui {

namespace {

QString typeText(plugins::PluginType type)
{
    switch (type) {
    case plugins::PluginType::Matcher:
        return QStringLiteral("Matcher: can move rows to another group (for example \"not this item\"), with its reason");
    case plugins::PluginType::Valuator:
        return QStringLiteral("Valuator: adds a figure next to the app's own numbers");
    case plugins::PluginType::Flagger:
        return QStringLiteral("Flagger: adds warnings or notes about the listing");
    case plugins::PluginType::Exporter:
        break;
    }
    return QStringLiteral("Exporter: saves a lookup as a file in your Downloads folder");
}

} // namespace

PluginsController::PluginsController(WorkFolder folder, const plugins::PluginCatalog &catalog, plugins::PluginHost &host,
                                     QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_catalog(catalog), m_host(host)
{
    refresh();
}

int PluginsController::timeLimitMs() const { return Settings(m_folder.settingsPath()).pluginCallTimeLimitMs(); }
int PluginsController::minTimeLimitMs() const { return Settings::kMinPluginCallTimeLimitMs; }
int PluginsController::maxTimeLimitMs() const { return Settings::kMaxPluginCallTimeLimitMs; }
QString PluginsController::pluginsFolder() const { return QDir::toNativeSeparators(m_folder.pluginsDir()); }

void PluginsController::refresh()
{
    m_problems.clear();
    m_plugins.clear();
    for (const plugins::PluginInfo &info : m_catalog.plugins(&m_problems)) {
        QStringList permissions;
        for (const QString &permission : info.manifest.permissions)
            permissions.append(plugins::permissionText(permission));
        m_plugins.append(QVariantMap{
            {QStringLiteral("id"), info.manifest.id},
            {QStringLiteral("name"), info.manifest.name},
            {QStringLiteral("version"), info.manifest.version},
            {QStringLiteral("author"), info.manifest.author},
            {QStringLiteral("description"), info.manifest.description},
            {QStringLiteral("type"), info.manifest.typeName()},
            {QStringLiteral("typeText"), typeText(info.manifest.type)},
            {QStringLiteral("permissions"), permissions},
            {QStringLiteral("builtIn"), info.builtIn},
            {QStringLiteral("enabled"), info.enabled},
            {QStringLiteral("failure"), info.failure},
        });
    }
    emit changed();
}

void PluginsController::reloadAll()
{
    m_host.reload();
    // A plugin that fails to load is disabled at once and shows why.
    m_host.takeFailures();
    refresh();
}

QString PluginsController::setEnabled(const QString &id, bool enabled)
{
    QString error;
    if (!m_catalog.setEnabled(id, enabled, &error))
        return error;
    reloadAll();
    for (const QVariant &entry : std::as_const(m_plugins)) {
        const QVariantMap plugin = entry.toMap();
        if (enabled && plugin.value(QStringLiteral("id")).toString() == id && !plugin.value(QStringLiteral("enabled")).toBool())
            return QStringLiteral("It could not be enabled: %1").arg(plugin.value(QStringLiteral("failure")).toString());
    }
    return {};
}

QString PluginsController::install(const QString &pathOrUrl)
{
    const QString text = pathOrUrl.trimmed();
    const QUrl url(text);
    const QString path = url.isLocalFile() ? url.toLocalFile() : text;
    QString error;
    const std::optional<plugins::PluginManifest> manifest = m_catalog.install(QDir::cleanPath(path), &error);
    if (!manifest)
        return QStringLiteral("Not installed: %1").arg(error);
    reloadAll();
    return {};
}

QString PluginsController::remove(const QString &id)
{
    QString error;
    const bool removed = m_catalog.remove(id, &error);
    reloadAll();
    return removed ? QString() : error;
}

QString PluginsController::setTimeLimitMs(int ms)
{
    Settings settings(m_folder.settingsPath());
    if (!settings.setPluginCallTimeLimitMs(ms))
        return QStringLiteral("The time limit must be %1 to %2 ms.").arg(Settings::kMinPluginCallTimeLimitMs).arg(Settings::kMaxPluginCallTimeLimitMs);
    if (!settings.sync())
        return QStringLiteral("The settings file cannot be written.");
    emit changed();
    return {};
}

} // namespace app::ui
