#include "lookup/KnownModels.h"

#include "core/PathChecks.h"
#include "lookup/Text.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace app::lookup {

namespace {

// The user's file holds a few hundred entries at most; anything far larger
// is not one.
constexpr qint64 kMaxUserFileBytes = 4 * 1024 * 1024;

QString simplifiedLower(const QString &text)
{
    return text.simplified().toLower();
}

// Reads the user's file into a JSON object: an empty object when the file
// does not exist; std::nullopt (and *error) when it cannot be used.
std::optional<QJsonObject> readUserObject(const QString &path, QString *error)
{
    if (isLink(path)) {
        *error = QStringLiteral("the known-models file %1 is a link; links are never followed").arg(path);
        return std::nullopt;
    }
    if (!somethingAt(path))
        return QJsonObject{{QStringLiteral("format"), 1}};
    QFile file(path);
    if (!isPlainFile(path) || QFileInfo(path).size() > kMaxUserFileBytes || !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("the known-models file %1 cannot be read").arg(path);
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() || document.object().value(QStringLiteral("format")).toInt() != 1) {
        *error = QStringLiteral("the known-models file %1 is damaged; it is left as it is").arg(path);
        return std::nullopt;
    }
    return document.object();
}

} // namespace

std::optional<KnownModels> KnownModels::builtIn(QString *error)
{
    QFile file(QStringLiteral(":/lookup/known-models.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("the built-in known-models data is missing");
        return std::nullopt;
    }
    return fromJson(file.readAll(), error);
}

std::optional<KnownModels> KnownModels::fromJson(const QByteArray &json, QString *error,
                                                 const QStringList &otherBrandIds)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    const QJsonObject root = document.object();
    if (!document.isObject() || root.value(QStringLiteral("format")).toInt() != 1) {
        *error = QStringLiteral("the known-models data is not a format-1 JSON object");
        return std::nullopt;
    }
    auto fail = [error](const QString &what) {
        *error = QStringLiteral("known-models data: %1").arg(what);
        return std::nullopt;
    };
    KnownModels data;
    for (const QJsonValue &value : root.value(QStringLiteral("brands")).toArray()) {
        const QJsonObject object = value.toObject();
        Brand brand;
        brand.id = object.value(QStringLiteral("id")).toString();
        if (brand.id.isEmpty())
            return fail(QStringLiteral("a brand has no id"));
        for (const QJsonValue &nameValue : object.value(QStringLiteral("names")).toArray()) {
            const QJsonObject name = nameValue.toObject();
            const QString text = name.value(QStringLiteral("name")).toString().simplified();
            if (text.isEmpty() || !name.value(QStringLiteral("search")).isBool())
                return fail(QStringLiteral("a name of brand %1 has no text or no search flag").arg(brand.id));
            brand.names.append({text, name.value(QStringLiteral("search")).toBool(),
                                name.value(QStringLiteral("onlyFor")).toString().simplified()});
        }
        if (brand.names.isEmpty())
            return fail(QStringLiteral("brand %1 has no name").arg(brand.id));
        data.m_brands.append(brand);
    }
    for (const QJsonValue &value : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject object = value.toObject();
        KnownModel model;
        model.brandId = object.value(QStringLiteral("brand")).toString();
        model.model = object.value(QStringLiteral("model")).toString().simplified();
        if (model.model.isEmpty() || (!data.brandById(model.brandId) && !otherBrandIds.contains(model.brandId)))
            return fail(QStringLiteral("a model has no code or an unknown brand"));
        const QJsonObject product = object.value(QStringLiteral("productName")).toObject();
        if (!product.isEmpty())
            model.productName = product.value(QStringLiteral("name")).toString().simplified();
        const QJsonObject category = object.value(QStringLiteral("category")).toObject();
        if (!category.isEmpty())
            model.category = category.value(QStringLiteral("name")).toString().simplified();
        // "searchFamily": {"value": true}: every family member
        // is searched too, not only matched (owner case 10).
        const QJsonObject searchFamily = object.value(QStringLiteral("searchFamily")).toObject();
        if (!searchFamily.isEmpty())
            model.searchFamily = searchFamily.value(QStringLiteral("value")).toBool();
        // "year": {"first": 1977, "last": 1993}; "last" is optional.
        const QJsonObject year = object.value(QStringLiteral("year")).toObject();
        if (!year.isEmpty()) {
            model.firstYear = year.value(QStringLiteral("first")).toInt();
            model.lastYear = year.value(QStringLiteral("last")).toInt();
            if (model.firstYear <= 0 || model.lastYear < 0 || (model.lastYear > 0 && model.lastYear < model.firstYear))
                return fail(QStringLiteral("the year of %1 is not a first year (and a later last year)").arg(model.model));
        }
        for (const QJsonValue &memberValue : object.value(QStringLiteral("family")).toArray()) {
            const QJsonObject member = memberValue.toObject();
            model.family.append(member.value(QStringLiteral("model")).toString().simplified());
        }
        for (const QJsonValue &noteValue : object.value(QStringLiteral("notes")).toArray()) {
            const QJsonObject note = noteValue.toObject();
            model.notes.append(note.value(QStringLiteral("text")).toString());
        }
        data.m_models.append(model);
    }
    const QJsonObject listings = root.value(QStringLiteral("listings")).toObject();
    for (auto it = listings.constBegin(); it != listings.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        data.m_confirmed.insert(it.key(), {entry.value(QStringLiteral("brand")).toString(),
                                           entry.value(QStringLiteral("model")).toString(),
                                           entry.value(QStringLiteral("productName")).toString()});
    }
    return data;
}

void KnownModels::add(const KnownModels &other)
{
    for (const Brand &brand : other.m_brands) {
        if (!brandById(brand.id))
            m_brands.append(brand);
    }
    for (const KnownModel &model : other.m_models) {
        if (!findModel(model.brandId, model.model))
            m_models.append(model);
    }
    for (auto it = other.m_confirmed.constBegin(); it != other.m_confirmed.constEnd(); ++it)
        m_confirmed.insert(it.key(), it.value());
}

bool KnownModels::addUserFile(const QString &path, QString *error)
{
    const std::optional<QJsonObject> object = readUserObject(path, error);
    if (!object)
        return false;
    const std::optional<KnownModels> data = fromJson(QJsonDocument(*object).toJson(), error, brandIds());
    if (!data)
        return false;
    add(*data);
    return true;
}

QStringList KnownModels::brandIds() const
{
    QStringList ids;
    for (const Brand &brand : m_brands)
        ids.append(brand.id);
    return ids;
}

bool KnownModels::saveConfirmed(const QString &path, const QString &itemId, const ConfirmedIdentity &identity,
                                QString *error)
{
    if (itemId.isEmpty() || identity.brand.simplified().isEmpty() || identity.model.simplified().isEmpty()) {
        *error = QStringLiteral("a listing, a brand and a model are needed");
        return false;
    }
    std::optional<QJsonObject> root = readUserObject(path, error);
    if (!root)
        return false;
    const QString brandText = identity.brand.simplified();
    const QString modelText = identity.model.simplified();
    QJsonArray brands = root->value(QStringLiteral("brands")).toArray();
    QJsonArray models = root->value(QStringLiteral("models")).toArray();
    QString brandId;
    if (const Brand *known = brandByName(brandText)) {
        brandId = known->id;
    } else {
        brandId = QStringLiteral("user:") + simplifiedLower(brandText);
        brands.append(QJsonObject{{QStringLiteral("id"), brandId},
                                  {QStringLiteral("names"), QJsonArray{QJsonObject{{QStringLiteral("name"), brandText},
                                                                                   {QStringLiteral("search"), true}}}}});
    }
    if (!findModel(brandId, modelText)) {
        QJsonObject model{{QStringLiteral("brand"), brandId},
                          {QStringLiteral("model"), modelText},
                          {QStringLiteral("family"), QJsonArray()},
                          {QStringLiteral("notes"), QJsonArray()}};
        if (!identity.productName.simplified().isEmpty())
            model.insert(QStringLiteral("productName"), QJsonObject{{QStringLiteral("name"), identity.productName.simplified()}});
        models.append(model);
    }
    QJsonObject listings = root->value(QStringLiteral("listings")).toObject();
    listings.insert(itemId, QJsonObject{{QStringLiteral("brand"), brandText},
                                        {QStringLiteral("model"), modelText},
                                        {QStringLiteral("productName"), identity.productName.simplified()}});
    root->insert(QStringLiteral("format"), 1);
    root->insert(QStringLiteral("brands"), brands);
    root->insert(QStringLiteral("models"), models);
    root->insert(QStringLiteral("listings"), listings);

    // Checked before writing: what is saved must read back.
    const std::optional<KnownModels> saved = fromJson(QJsonDocument(*root).toJson(), error, brandIds());
    if (!saved)
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(*root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        *error = QStringLiteral("the known-models file %1 cannot be saved: %2").arg(path, file.errorString());
        return false;
    }
    add(*saved);
    return true;
}

const Brand *KnownModels::brandById(const QString &id) const
{
    for (const Brand &brand : m_brands) {
        if (brand.id == id)
            return &brand;
    }
    return nullptr;
}

const Brand *KnownModels::brandByName(const QString &name) const
{
    const QString wanted = simplifiedLower(name);
    for (const Brand &brand : m_brands) {
        for (const BrandName &entry : brand.names) {
            if (simplifiedLower(entry.name) == wanted)
                return &brand;
        }
    }
    return nullptr;
}

QList<const KnownModel *> KnownModels::modelsOf(const QString &brandId) const
{
    QList<const KnownModel *> result;
    for (const KnownModel &model : m_models) {
        if (model.brandId == brandId)
            result.append(&model);
    }
    return result;
}

const KnownModel *KnownModels::findModel(const QString &brandId, const QString &code) const
{
    const QString wanted = normalizeCode(code);
    if (wanted.isEmpty())
        return nullptr;
    for (const KnownModel &model : m_models) {
        if (model.brandId != brandId)
            continue;
        if (normalizeCode(model.model) == wanted)
            return &model;
        for (const QString &member : model.family) {
            if (normalizeCode(member) == wanted)
                return &model;
        }
    }
    return nullptr;
}

std::optional<ConfirmedIdentity> KnownModels::confirmedFor(const QString &itemId) const
{
    const auto it = m_confirmed.constFind(itemId);
    if (it == m_confirmed.constEnd())
        return std::nullopt;
    return it.value();
}

} // namespace app::lookup
