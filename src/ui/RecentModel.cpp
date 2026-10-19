#include "ui/RecentModel.h"

#include "ui/Format.h"

namespace app::ui {

namespace {

QString nameOf(const lookup::LookupSummary &item)
{
    const QString name = QStringLiteral("%1 %2").arg(item.brand, item.model).simplified();
    if (!item.brand.isEmpty() && !item.model.isEmpty())
        return name;
    if (!item.title.isEmpty())
        return item.title;
    return item.itemId.isEmpty() ? QStringLiteral("Unknown listing") : QStringLiteral("Item %1").arg(item.itemId);
}

} // namespace

RecentModel::RecentModel(QObject *parent) : QAbstractListModel(parent) {}

bool RecentModel::refresh(const lookup::LookupStore &store, QString *error)
{
    const std::optional<QList<lookup::LookupSummary>> items = store.recent(kLimit, error);
    if (!items)
        return false;
    beginResetModel();
    m_items = *items;
    endResetModel();
    emit countChanged();
    return true;
}

int RecentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant RecentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const lookup::LookupSummary &item = m_items.at(index.row());
    switch (role) {
    case LookupIdRole:
        return item.id;
    case NameRole:
        return nameOf(item);
    case CheckedRole:
        return QStringLiteral("checked ") + whenText(item.createdAt, QDateTime::currentDateTime());
    case FlagTextRole:
    case FlagKindRole: {
        QString text;
        QString kind;
        if (item.status == QLatin1String("partial")) {
            text = QStringLiteral("Cancelled before the end");
            kind = QStringLiteral("warn");
        } else if (item.status == QLatin1String("failed")) {
            text = QStringLiteral("Stopped before the end");
            kind = QStringLiteral("warn");
        } else if (item.shippingFlagged) {
            text = item.shippingFlagRatio
                       ? QStringLiteral("Shipping %1x normal").arg(QString::number(*item.shippingFlagRatio, 'f', 1))
                       : QStringLiteral("Shipping above normal");
            kind = QStringLiteral("warn");
        } else if (item.cheaperForSale > 0) {
            text = QStringLiteral("%1 cheaper for sale").arg(item.cheaperForSale);
            kind = QStringLiteral("info");
        }
        return role == FlagTextRole ? QVariant(text) : QVariant(kind);
    }
    case ListingTotalRole:
        return moneyText(item.listingTotalCents);
    case SoldMedianRole:
        return moneyText(item.soldMedianCents);
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentModel::roleNames() const
{
    return {{LookupIdRole, "lookupId"},   {NameRole, "name"},           {CheckedRole, "checked"},
            {FlagTextRole, "flagText"},   {FlagKindRole, "flagKind"},   {ListingTotalRole, "listingTotal"},
            {SoldMedianRole, "soldMedian"}};
}

} // namespace app::ui
