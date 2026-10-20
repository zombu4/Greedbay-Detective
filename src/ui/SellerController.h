#pragma once

#include "core/WorkFolder.h"
#include "lookup/SellerStore.h"
#include "ui/SellerSession.h"

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace app::ui {

// The seller panel (owner decisions 2026-10-20): opened by a click on a
// seller name on Results. Shows the rating the page showed, the user's own
// mark (mark, edit, unmark: local only), and on request the seller's
// negative or neutral reviews (SellerSession reads them from eBay's classic
// feedback page; a read within Settings' cache days is shown without a page
// load). Also the "My marked sellers" list of Settings.
class SellerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the backend")
    Q_PROPERTY(QString sellerId READ sellerId NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    // "99.8% positive, 3,295 ratings", or "-" when the page showed none.
    Q_PROPERTY(QString ratingText READ ratingText NOTIFY changed)
    Q_PROPERTY(QString ratingNote READ ratingNote NOTIFY changed)
    Q_PROPERTY(bool ratingFlagged READ ratingFlagged NOTIFY changed)
    Q_PROPERTY(bool marked READ marked NOTIFY changed)
    Q_PROPERTY(QString markReason READ markReason NOTIFY changed)
    Q_PROPERTY(QString markReasonLabel READ markReasonLabel NOTIFY changed)
    Q_PROPERTY(QString markNote READ markNote NOTIFY changed)
    Q_PROPERTY(QString markDate READ markDate NOTIFY changed)
    // [{id, label}] in the order the dialog shows them.
    Q_PROPERTY(QVariantList reasons READ reasons CONSTANT)
    // The reviews shown: kind "negative" or "neutral" (empty: none shown yet),
    // [{when, comment, item, price, verified}], a summary of the counts and
    // when they were read.
    Q_PROPERTY(QString reviewKind READ reviewKind NOTIFY changed)
    Q_PROPERTY(QVariantList reviews READ reviews NOTIFY changed)
    Q_PROPERTY(QString reviewSummary READ reviewSummary NOTIFY changed)
    Q_PROPERTY(QString readText READ readText NOTIFY changed)
    Q_PROPERTY(SellerSession *session READ session CONSTANT)
    // Settings "My marked sellers": [{sellerId, reason, reasonLabel, note, date}].
    Q_PROPERTY(QVariantList marks READ marks NOTIFY marksChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    SellerController(WorkFolder folder, const QString &connectionName, std::function<bool()> lookupRunning,
                     QObject *parent = nullptr);

    QString sellerId() const { return m_sellerId; }
    QString displayName() const { return m_displayName; }
    QString ratingText() const;
    QString ratingNote() const;
    bool ratingFlagged() const;
    bool marked() const { return m_mark.has_value(); }
    QString markReason() const { return m_mark ? m_mark->reason : QString(); }
    QString markReasonLabel() const;
    QString markNote() const { return m_mark ? m_mark->note : QString(); }
    QString markDate() const { return m_mark ? m_mark->markedOn.toString(Qt::ISODate) : QString(); }
    QVariantList reasons() const;
    QString reviewKind() const { return m_feedback ? m_feedback->kind : QString(); }
    QVariantList reviews() const;
    QString reviewSummary() const;
    QString readText() const;
    SellerSession *session() const { return m_session; }
    QVariantList marks() const { return m_marks; }
    QString error() const { return m_error; }

    // Opens the panel for a seller; percent / count as the page showed them
    // (-1: not shown).
    Q_INVOKABLE void open(const QString &sellerId, const QString &displayName, double percent, int count);
    // Marks (or re-marks) the panel's seller; false and error when refused.
    Q_INVOKABLE bool saveMark(const QString &reason, const QString &note);
    // Settings: edit the mark of any marked seller.
    Q_INVOKABLE bool saveMarkFor(const QString &sellerId, const QString &reason, const QString &note);
    Q_INVOKABLE bool unmark(const QString &sellerId);
    // Shows the reviews of `kind`: the stored read when it is newer than the
    // cache days (and refresh is false), else reads the page (one load).
    Q_INVOKABLE void showReviews(const QString &kind, bool refresh);
    Q_INVOKABLE void refreshMarks();

signals:
    void changed();
    void marksChanged();

private:
    void loadMark();
    void setError(const QString &error);

    WorkFolder m_folder;
    lookup::SellerStore m_store;
    std::function<bool()> m_lookupRunning;
    SellerSession *m_session;
    QString m_sellerId;
    QString m_displayName;
    std::optional<double> m_percent;
    std::optional<int> m_count;
    std::optional<lookup::SellerMark> m_mark;
    std::optional<lookup::SellerFeedback> m_feedback;
    QVariantList m_marks;
    QString m_error;
};

} // namespace app::ui
