#pragma once

#include "core/Settings.h"
#include "lookup/LookupStore.h"

#include <QAbstractListModel>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace app::ui {

// One table of the Results screen (ui-spec 2): the rows of one tab, sorted
// by total, lowest first (rows without a total last). The rows of a tab never
// change while a lookup is shown; ticking a box only changes `included` and
// the CHEAPEST marks, which are recomputed for every row.
class RowsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        RowRole = Qt::UserRole + 1, // index into the lookup's rows (for setIncluded)
        IncludedRole,
        WhereRole,      // "sold", "sold, price not confirmed", "for sale", "research"
        TotalRole,      // "$123.45" or "-"
        ConditionRole,  // eBay's condition text, as eBay gives it
        DateRole,       // sold date, Product Research's last sold date, or "-"
        TitleRole,
        ItemIdRole,
        ReasonRole,     // why it is not counted by default (the not-this-item tab)
        MarkRole,       // "this" (THIS LISTING), "cheapest" (CHEAPEST) or ""
    };

    struct Entry {
        int row = 0;
        bool included = false;
        QString where;
        QString total;
        QString condition;
        QString date;
        QString title;
        QString itemId;
        QString reason;
        QString mark;
    };

    explicit RowsModel(QObject *parent = nullptr);

    // A new list (other rows): the model is reset.
    void reset(const QList<Entry> &entries);
    // The same rows with new checkboxes and marks: dataChanged for all.
    void update(const QList<Entry> &entries);

    int count() const { return static_cast<int>(m_entries.size()); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<Entry> m_entries;
};

// What the Results screen shows (ui-spec 2) for one saved lookup: the
// identity bar, the listing card, the flag cards, the sold numbers with the
// range bar, Product Research and eBay's own summary as side lines, and the
// two tabs. All text is ready to show; QML only lays it out.
//
// The numbers come from lookup::valuate() over the counted rows. Ticking or
// unticking a row (setIncluded) recomputes everything at once and stores the
// choice and the new numbers in the database (LookupStore::updateIncluded).
class ResultsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    Q_PROPERTY(bool hasLookup READ hasLookup NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString statusKind READ statusKind NOTIFY changed)
    Q_PROPERTY(QString storeError READ storeError NOTIFY changed)
    // Identity bar.
    Q_PROPERTY(QString identityName READ identityName NOTIFY changed)
    Q_PROPERTY(QString identitySource READ identitySource NOTIFY changed)
    Q_PROPERTY(QString searches READ searches NOTIFY changed)
    Q_PROPERTY(QStringList identityNotes READ identityNotes NOTIFY changed)
    Q_PROPERTY(QString brand READ brand NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString productName READ productName NOTIFY changed)
    // Listing card.
    Q_PROPERTY(QString itemId READ itemId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString condition READ condition NOTIFY changed)
    Q_PROPERTY(QString seller READ seller NOTIFY changed)
    Q_PROPERTY(QString priceText READ priceText NOTIFY changed)
    Q_PROPERTY(QString shippingText READ shippingText NOTIFY changed)
    Q_PROPERTY(QString totalText READ totalText NOTIFY changed)
    Q_PROPERTY(QString photoSource READ photoSource NOTIFY changed)
    // Flag cards.
    Q_PROPERTY(bool shippingFlag READ shippingFlag NOTIFY changed)
    Q_PROPERTY(QStringList shippingLines READ shippingLines NOTIFY changed)
    Q_PROPERTY(bool cheaperFlag READ cheaperFlag NOTIFY changed)
    Q_PROPERTY(QString cheaperLine READ cheaperLine NOTIFY changed)
    Q_PROPERTY(QString cheapestItemId READ cheapestItemId NOTIFY changed)
    // "What the same item really sold for".
    Q_PROPERTY(QString soldLine READ soldLine NOTIFY changed)
    Q_PROPERTY(bool hasSoldNumbers READ hasSoldNumbers NOTIFY changed)
    Q_PROPERTY(QString medianText READ medianText NOTIFY changed)
    Q_PROPERTY(QString middleText READ middleText NOTIFY changed)
    Q_PROPERTY(QString lowHighText READ lowHighText NOTIFY changed)
    Q_PROPERTY(QString forSaleText READ forSaleText NOTIFY changed)
    // Product Research, with its period (owner decision 2026-10-19: 3 years
    // by default): a heading line and, when it counted sales, its numbers.
    Q_PROPERTY(QString researchLine READ researchLine NOTIFY changed)
    Q_PROPERTY(bool hasResearchNumbers READ hasResearchNumbers NOTIFY changed)
    Q_PROPERTY(QString researchMedianText READ researchMedianText NOTIFY changed)
    Q_PROPERTY(QString researchMiddleText READ researchMiddleText NOTIFY changed)
    Q_PROPERTY(QString researchLowHighText READ researchLowHighText NOTIFY changed)
    // Which numbers the range bar shows: the sold search's, or Product
    // Research's when the sold search counted none.
    Q_PROPERTY(QString barSource READ barSource NOTIFY changed)
    // The range bar, in cents; -1 = not shown. barMin/barMax span every value
    // shown on it.
    Q_PROPERTY(double barMin READ barMin NOTIFY changed)
    Q_PROPERTY(double barMax READ barMax NOTIFY changed)
    Q_PROPERTY(double bandLow READ bandLow NOTIFY changed)
    Q_PROPERTY(double bandHigh READ bandHigh NOTIFY changed)
    Q_PROPERTY(double medianValue READ medianValue NOTIFY changed)
    Q_PROPERTY(double thisValue READ thisValue NOTIFY changed)
    Q_PROPERTY(double cheapestValue READ cheapestValue NOTIFY changed)
    Q_PROPERTY(QString cheapestLabel READ cheapestLabel NOTIFY changed)
    // Owner case 10: every search found nothing (no sold, no for sale, no
    // research row) -> this sentence instead of empty tables; else empty.
    Q_PROPERTY(QString emptyMessage READ emptyMessage NOTIFY changed)
    // Side lines.
    Q_PROPERTY(QStringList sideLines READ sideLines NOTIFY changed)
    // Tabs.
    Q_PROPERTY(RowsModel *sameItem READ sameItem CONSTANT)
    Q_PROPERTY(RowsModel *notThisItem READ notThisItem CONSTANT)
public:
    // `photoSourceOf` gives the image source of a listing's saved photo, or
    // an empty string when there is none.
    ResultsController(lookup::LookupStore store, std::function<ValueSettings()> valueSettings,
                      std::function<QString(const QString &itemId)> photoSourceOf, QObject *parent = nullptr);

    // Shows a saved lookup; false (and *error) when it cannot be read.
    bool show(qint64 lookupId, QString *error);
    // The photo of the shown listing was saved or changed.
    void photoChanged();

    // The lookup shown (for "Wrong item? Correct it").
    const lookup::SavedLookup &saved() const { return m_saved; }

    // Ticks or unticks row `row` (RowsModel::RowRole).
    Q_INVOKABLE void setIncluded(int row, bool included);

    bool hasLookup() const { return m_saved.id != 0; }
    QString statusText() const;
    QString statusKind() const;
    QString storeError() const { return m_storeError; }
    QString identityName() const;
    QString identitySource() const;
    QString searches() const;
    QStringList identityNotes() const;
    QString brand() const { return m_saved.result.identity.brandName; }
    QString model() const { return m_saved.result.identity.model; }
    QString productName() const { return m_saved.result.identity.productName; }
    QString itemId() const { return m_saved.result.item.itemId; }
    QString title() const { return m_saved.result.item.title; }
    QString condition() const { return m_saved.result.item.condition; }
    QString seller() const { return m_saved.result.item.sellerName; }
    QString priceText() const;
    QString shippingText() const;
    QString totalText() const;
    QString photoSource() const { return m_photoSource; }
    bool shippingFlag() const { return m_valuation.shipping.flagged; }
    QStringList shippingLines() const;
    bool cheaperFlag() const;
    QString cheaperLine() const;
    QString cheapestItemId() const { return m_cheapestItemId; }
    QString soldLine() const;
    bool hasSoldNumbers() const { return m_valuation.sold.medianCents.has_value(); }
    QString medianText() const;
    QString middleText() const;
    QString lowHighText() const;
    QString forSaleText() const;
    QString researchLine() const;
    bool hasResearchNumbers() const { return m_valuation.research.medianCents.has_value(); }
    QString researchMedianText() const;
    QString researchMiddleText() const;
    QString researchLowHighText() const;
    QString barSource() const;
    double barMin() const { return m_bar.min; }
    double barMax() const { return m_bar.max; }
    double bandLow() const { return m_bar.bandLow; }
    double bandHigh() const { return m_bar.bandHigh; }
    double medianValue() const { return m_bar.median; }
    double thisValue() const { return m_bar.thisTotal; }
    double cheapestValue() const { return m_bar.cheapest; }
    QString cheapestLabel() const;
    QStringList sideLines() const;
    QString emptyMessage() const;
    RowsModel *sameItem() const { return m_sameItem; }
    RowsModel *notThisItem() const { return m_notThisItem; }

signals:
    void changed();
    // The user's choice and the new numbers were stored in the database.
    void stored();

private:
    // Valuation, CHEAPEST marks, the cheapest listing and the bar, from the
    // rows as they are now.
    void recompute();
    QList<RowsModel::Entry> entries(bool sameItem) const;
    // "last 3 years", "last 2 years", "last 90 days".
    QString periodText() const;

    lookup::LookupStore m_store;
    std::function<ValueSettings()> m_valueSettings;
    std::function<QString(const QString &)> m_photoSourceOf;
    lookup::SavedLookup m_saved;
    lookup::Valuation m_valuation;
    QString m_photoSource;
    QString m_storeError;
    // Indexes into m_saved.rows of the rows marked CHEAPEST.
    QSet<int> m_cheapest;
    QString m_cheapestItemId;
    std::optional<qint64> m_cheapestTotal;
    struct Bar {
        double min = -1;
        double max = -1;
        double bandLow = -1;
        double bandHigh = -1;
        double median = -1;
        double thisTotal = -1;
        double cheapest = -1;
    } m_bar;
    RowsModel *m_sameItem;
    RowsModel *m_notThisItem;
};

} // namespace app::ui
