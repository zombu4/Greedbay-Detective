#pragma once

#include "lookup/History.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupStore.h"
#include "lookup/MatchWords.h"

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace app::ui {

// The Price history screen (ui-spec 3): pills to pick a checked item, its
// sold prices over time in a window of 90 days, 1 year or 3 years (dots =
// each sale's total, the middle half as a band, the median), the user's own
// checks of the item as amber dots and as a table, and "Re-check now" (a new
// lookup of the item's newest listing; older lookups stay).
//
// Chart values: times in ms since the epoch (UTC), money in cents; -1 = not
// shown. The view places them between the minimum and maximum given here.
class HistoryController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    // [{key, name}] of every checked item, the most recently checked first.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY changed)
    Q_PROPERTY(int windowDays READ windowDays NOTIFY changed)
    Q_PROPERTY(QString heading READ heading NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    // The header's tiles (the same look as the Results numbers, ui-spec 2);
    // empty when the window has no sale.
    Q_PROPERTY(QString medianText READ medianText NOTIFY changed)
    Q_PROPERTY(QString middleText READ middleText NOTIFY changed)
    Q_PROPERTY(QString salesText READ salesText NOTIFY changed)
    // [{time, cents, label}]: each sale in the window, oldest first.
    Q_PROPERTY(QVariantList sales READ sales NOTIFY changed)
    // [{lookupId, time, cents, checked, itemId, total, medianThen, flags}]:
    // the user's checks of the item, newest first; `cents` -1 without a total.
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(double minTime READ minTime NOTIFY changed)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY changed)
    Q_PROPERTY(double minCents READ minCents NOTIFY changed)
    Q_PROPERTY(double maxCents READ maxCents NOTIFY changed)
    Q_PROPERTY(double bandLow READ bandLow NOTIFY changed)
    Q_PROPERTY(double bandHigh READ bandHigh NOTIFY changed)
    Q_PROPERTY(double median READ median NOTIFY changed)
    Q_PROPERTY(QString medianLabel READ medianLabel NOTIFY changed)
    Q_PROPERTY(QString startLabel READ startLabel NOTIFY changed)
    Q_PROPERTY(QString endLabel READ endLabel NOTIFY changed)
    Q_PROPERTY(QString lowLabel READ lowLabel NOTIFY changed)
    Q_PROPERTY(QString highLabel READ highLabel NOTIFY changed)
    // The item's newest lookup has a link or item number to check again.
    Q_PROPERTY(bool canRecheck READ canRecheck NOTIFY changed)
    // Why the screen shows nothing, or the error reading the database.
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    // The windows the screen offers, in days: 90 days, 1 year, 3 years.
    static constexpr int kWindows[] = {90, 365, 1095};

    // `store`, `known` and `words` must outlive the controller; `known` and
    // `words` may be null (the build's data cannot be read: nothing shown).
    HistoryController(const lookup::LookupStore &store, const lookup::KnownModels *known,
                      const lookup::MatchWords *words, QObject *parent = nullptr);

    QVariantList items() const { return m_items; }
    QString currentKey() const { return m_currentKey; }
    int windowDays() const { return m_windowDays; }
    QString heading() const { return m_heading; }
    QString summary() const { return m_summary; }
    QString medianText() const { return m_medianText; }
    QString middleText() const { return m_middleText; }
    QString salesText() const { return m_salesText; }
    QVariantList sales() const { return m_sales; }
    QVariantList checks() const { return m_checks; }
    double minTime() const { return m_minTime; }
    double maxTime() const { return m_maxTime; }
    double minCents() const { return m_minCents; }
    double maxCents() const { return m_maxCents; }
    double bandLow() const { return m_bandLow; }
    double bandHigh() const { return m_bandHigh; }
    double median() const { return m_median; }
    QString medianLabel() const { return m_medianLabel; }
    QString startLabel() const { return m_startLabel; }
    QString endLabel() const { return m_endLabel; }
    QString lowLabel() const { return m_lowLabel; }
    QString highLabel() const { return m_highLabel; }
    bool canRecheck() const { return !m_recheckInput.isEmpty(); }
    QString message() const { return m_message; }

    // Reads the saved lookups again (after a lookup is saved or the user's
    // ticks change); keeps the picked item when it is still there.
    Q_INVOKABLE void refresh();
    // Picks an item (a key of `items`).
    Q_INVOKABLE void select(const QString &key);
    // Picks the window: one of kWindows; anything else is ignored.
    Q_INVOKABLE void setWindow(int days);
    // Asks for a new lookup of the picked item's newest listing
    // (recheckRequested).
    Q_INVOKABLE void recheck();

signals:
    void changed();
    // A new lookup of `input` should run (Backend starts it).
    void recheckRequested(const QString &input);

private:
    void rebuild();

    const lookup::LookupStore &m_store;
    const lookup::KnownModels *m_known;
    const lookup::MatchWords *m_words;
    QList<lookup::HistoryItem> m_all;
    QVariantList m_items;
    QString m_currentKey;
    int m_windowDays = kWindows[0];
    QString m_heading;
    QString m_summary;
    QString m_medianText;
    QString m_middleText;
    QString m_salesText;
    QVariantList m_sales;
    QVariantList m_checks;
    double m_minTime = 0;
    double m_maxTime = 0;
    double m_minCents = 0;
    double m_maxCents = 0;
    double m_bandLow = -1;
    double m_bandHigh = -1;
    double m_median = -1;
    QString m_medianLabel;
    QString m_startLabel;
    QString m_endLabel;
    QString m_lowLabel;
    QString m_highLabel;
    QString m_recheckInput;
    QString m_message;
};

} // namespace app::ui
