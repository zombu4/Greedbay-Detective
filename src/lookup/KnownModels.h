#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::lookup {

// Brands and models the lookup knows (owner rules 23, 25, 27, 28):
//   - built in: data/known-models.json (:/lookup/known-models.json);
//   - the user's additions: <work>/known-models.json, the same format plus
//     "listings": the identity the user confirmed for each listing (rule 7).
//     Written only by saveConfirmed().
//
// A brand has several names (aliases, successor names as sellers write
// them); a name with "search" true is used in search queries, every name is
// accepted in result titles. A model has the manufacturer's spelling, an
// optional product name ("Z Meter"), a family (suffix versions that count as
// the same item, owner rule 28; a model without one keeps its versions
// apart), known-issue notes (rule 25) and the years it was made.
struct BrandName {
    QString name;
    bool search = true;
    // When set, the name applies only to models of this category (owner
    // decision 2026-10-19: Agilent/Keysight only for HP test equipment).
    QString onlyFor;
};

struct Brand {
    QString id;
    QList<BrandName> names;
};

struct KnownModel {
    QString brandId;
    QString model;
    QString productName;
    QString category; // e.g. "test equipment"; empty when not recorded
    QStringList family;
    // The family members are searched too (one query each), not only
    // matched; off unless the data says so (owner rule 21, case 10).
    bool searchFamily = false;
    QStringList notes;
    // The first and, when recorded, the last year the model was made or
    // sold (owner decision 2026-10-19); 0 when not recorded.
    int firstYear = 0;
    int lastYear = 0;
};

// What the user confirmed for one listing.
struct ConfirmedIdentity {
    QString brand; // as the user wrote it
    QString model;
    QString productName;
};

class KnownModels {
public:
    // The built-in data. std::nullopt (and *error) when it cannot be read.
    static std::optional<KnownModels> builtIn(QString *error);
    // Data in the file format; "listings" are read when present. A model
    // may name a brand of the file or one of `otherBrandIds` (the user's file
    // adds models to built-in brands).
    static std::optional<KnownModels> fromJson(const QByteArray &json, QString *error,
                                               const QStringList &otherBrandIds = {});

    // Adds the user's file at `path`. A missing file adds nothing and is no
    // error. A link, an unreadable or damaged file is refused (and *error):
    // nothing of it is used.
    bool addUserFile(const QString &path, QString *error);

    // Saves what the user confirmed for `itemId` in the user's file at
    // `path` and adds it here: the listing's identity, the brand (when no
    // brand has that name) and the model (when the brand has no model of that
    // normalized code). The file is rewritten as a whole (never through a
    // link); a damaged file is not overwritten.
    bool saveConfirmed(const QString &path, const QString &itemId, const ConfirmedIdentity &identity, QString *error);

    // Pointers returned below stay valid until the next addUserFile() or
    // saveConfirmed().
    const QList<Brand> &brands() const { return m_brands; }
    const Brand *brandById(const QString &id) const;
    // The brand one of whose names equals `name` (case and runs of spaces
    // ignored), or nullptr.
    const Brand *brandByName(const QString &name) const;
    QList<const KnownModel *> modelsOf(const QString &brandId) const;
    // The model of `brandId` whose code or a family member's code equals
    // `code` after normalizeCode(), or nullptr.
    const KnownModel *findModel(const QString &brandId, const QString &code) const;
    std::optional<ConfirmedIdentity> confirmedFor(const QString &itemId) const;

private:
    void add(const KnownModels &other);
    QStringList brandIds() const;

    QList<Brand> m_brands;
    QList<KnownModel> m_models;
    QHash<QString, ConfirmedIdentity> m_confirmed;
};

} // namespace app::lookup
