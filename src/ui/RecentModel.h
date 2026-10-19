#pragma once

#include "lookup/LookupStore.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

namespace app::ui {

// The recent lookups on the Home screen (ui-spec 1): one card per saved
// lookup, newest first, with the item name, when it was checked, one flag
// pill (warn: shipping flagged; info: cheaper listings for sale; warn: the
// lookup did not complete), the listing total and the sold median.
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
    };

    explicit RecentModel(QObject *parent = nullptr);

    // Reads the list again; *error when the database cannot be read.
    bool refresh(const lookup::LookupStore &store, QString *error);

    int count() const { return static_cast<int>(m_items.size()); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<lookup::LookupSummary> m_items;
};

} // namespace app::ui
