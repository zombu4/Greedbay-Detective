#include "plugins/PluginManifest.h"

#include "core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace app::plugins {

namespace {

const struct {
    PluginType type;
    const char *name;
    const char *function;
} kTypes[] = {
    {PluginType::Matcher, "matcher", "match"},
    {PluginType::Valuator, "valuator", "value"},
    {PluginType::Flagger, "flagger", "flags"},
    {PluginType::Exporter, "exporter", "exportLookup"},
};

const char *const kPermissions[] = {"listing", "identity", "rows", "export"};

const char *const kFields[] = {"id", "name", "version", "author", "description", "pluginApiVersion", "type", "permissions"};

// The text at `key`, simplified, 1..maxLength characters (0..maxLength when
// optional); std::nullopt otherwise.
std::optional<QString> textField(const QJsonObject &root, const char *key, qsizetype maxLength, bool optional)
{
    const QJsonValue value = root.value(QLatin1String(key));
    if (value.isUndefined() && optional)
        return QString();
    if (!value.isString())
        return std::nullopt;
    const QString text = value.toString().simplified();
    if ((text.isEmpty() && !optional) || text.size() > maxLength)
        return std::nullopt;
    return text;
}

} // namespace

QString PluginManifest::function() const
{
    for (const auto &entry : kTypes) {
        if (entry.type == type)
            return QLatin1String(entry.function);
    }
    return {};
}

QString PluginManifest::typeName() const
{
    for (const auto &entry : kTypes) {
        if (entry.type == type)
            return QLatin1String(entry.name);
    }
    return {};
}

std::optional<PluginManifest> PluginManifest::fromJson(const QByteArray &json, QString *error)
{
    auto fail = [error](const QString &what) {
        *error = QStringLiteral("manifest.json: %1").arg(what);
        return std::nullopt;
    };
    if (json.size() > kMaxManifestBytes)
        return fail(QStringLiteral("larger than %1 KB").arg(kMaxManifestBytes / 1024));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("not a JSON object"));
    const QJsonObject root = document.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (std::none_of(std::begin(kFields), std::end(kFields), [&it](const char *field) { return it.key() == QLatin1String(field); }))
            return fail(QStringLiteral("unknown field \"%1\"").arg(it.key()));
    }
    PluginManifest manifest;
    const QJsonValue apiVersion = root.value(QStringLiteral("pluginApiVersion"));
    if (!apiVersion.isDouble() || apiVersion.toDouble() != kPluginApiVersion)
        return fail(QStringLiteral("pluginApiVersion must be %1 (this app runs plugin API version %1)").arg(kPluginApiVersion));
    manifest.id = root.value(QStringLiteral("id")).toString();
    if (!Settings::isPluginId(manifest.id))
        return fail(QStringLiteral("id must be 1-64 characters of a-z, 0-9 and \"-\""));
    const std::optional<QString> name = textField(root, "name", 80, false);
    const std::optional<QString> version = textField(root, "version", 32, false);
    const std::optional<QString> author = textField(root, "author", 80, false);
    const std::optional<QString> description = textField(root, "description", 500, true);
    if (!name || !version || !author)
        return fail(QStringLiteral("name, version and author must be text (name and author up to 80 characters, "
                                   "version up to 32)"));
    if (!description)
        return fail(QStringLiteral("description must be text of up to 500 characters"));
    manifest.name = *name;
    manifest.version = *version;
    manifest.author = *author;
    manifest.description = *description;
    const QString type = root.value(QStringLiteral("type")).toString();
    bool knownType = false;
    for (const auto &entry : kTypes) {
        if (type == QLatin1String(entry.name)) {
            manifest.type = entry.type;
            knownType = true;
        }
    }
    if (!knownType)
        return fail(QStringLiteral("type must be \"matcher\", \"valuator\", \"flagger\" or \"exporter\""));
    const QJsonValue permissions = root.value(QStringLiteral("permissions"));
    if (!permissions.isArray())
        return fail(QStringLiteral("permissions must be a list"));
    for (const QJsonValue &value : permissions.toArray()) {
        const QString permission = value.toString();
        if (std::none_of(std::begin(kPermissions), std::end(kPermissions),
                         [&permission](const char *known) { return permission == QLatin1String(known); }))
            return fail(QStringLiteral("unknown permission \"%1\"").arg(value.toVariant().toString()));
        if (!manifest.permissions.contains(permission))
            manifest.permissions.append(permission);
    }
    const bool exports = manifest.allows(QStringLiteral("export"));
    if ((manifest.type == PluginType::Exporter) != exports)
        return fail(QStringLiteral("the \"export\" permission is for exporters only, and every exporter needs it"));
    if ((manifest.type == PluginType::Matcher || manifest.type == PluginType::Valuator) && !manifest.allows(QStringLiteral("rows")))
        return fail(QStringLiteral("matchers and valuators need the \"rows\" permission"));
    return manifest;
}

QString permissionText(const QString &permission)
{
    if (permission == QLatin1String("listing"))
        return QStringLiteral("Reads the listing you checked: title, price, shipping, condition, format, seller name and "
                              "ratings, item number and item specifics.");
    if (permission == QLatin1String("identity"))
        return QStringLiteral("Reads what the listing was identified as: brand, model, product name and category.");
    if (permission == QLatin1String("rows"))
        return QStringLiteral("Reads the sold, for-sale and Product Research rows: titles, totals, eBay conditions, dates, "
                              "sellers, and whether each row is counted.");
    if (permission == QLatin1String("export"))
        return QStringLiteral("Saves a file in your Downloads folder, through the app, when you choose it.");
    return permission;
}

} // namespace app::plugins
