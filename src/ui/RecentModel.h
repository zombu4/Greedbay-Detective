#pragma once

#include "lookup/LookupStore.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

namespace app::ui {

// The recent lookups on the Home screen (ui-spec 1): one card per saved
// lookup, newest first, with the item name, when it was checked, one flag
// pill (warn: shipping flagged; info: cheaper listings for sale; warn: the
// lookup did not complete), a "Likely broken" pill when that applies (owner
// decision 2026-10-19), the listing total and the sold median.
class RecentModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    // How many recent lookups the Home screen lists.
    static constexpr int kLimit = 50;

    enum Roles {
        LookupIdRole = Qt::UserRole + 1,
        NameRole,
        CheckedRole,
        FlagTextRole,
        FlagKindRole, // "warn", "info" or ""
        ListingTotalRole,
        SoldMedianRole,
        BrokenRole, // "Likely broken" or ""
    };

    explicit RecentModel(QObject *parent = nullptr);

    // Reads the list again; *error when the database cannot be read.
    // `words` (may be null: no "Likely broken" pill) must outlive the model.
    bool refresh(const lookup::LookupStore &store, const lookup::MatchWords *words, QString *error);

    int count() const { return static_cast<int>(m_items.size()); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<lookup::LookupSummary> m_items;
    const lookup::MatchWords *m_words = nullptr;
};

} // namespace app::ui
