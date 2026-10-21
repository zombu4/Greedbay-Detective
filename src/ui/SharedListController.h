#pragma once

#include "core/WorkFolder.h"
#include "sellerlist/ListClient.h"
#include "ui/PurchaseCheck.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

namespace app::ui {

// The shared seller list in the app (DESIGN 3.7; owner decisions 2026-10-20):
// opt-in reading and reporting (Settings, both off by default), everything
// over HTTPS to the list owner's server (ListClient). Reading: at most once a
// day at start-up, plus "Update now". Reporting: the purchase check first
// (the user's own Purchases page in the app's browser), then only the seller
// id, the reason, the user's text, the item number and the month are sent,
// sealed to the server's key. When the server cannot be reached the list is
// "not updated" and the saved copy stays.
class SharedListController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the backend")
    // This build carries the list owner's keys and address (data/master.json).
    Q_PROPERTY(bool setUp READ setUp CONSTANT)
    Q_PROPERTY(bool readEnabled READ readEnabled NOTIFY changed)
    Q_PROPERTY(bool reportEnabled READ reportEnabled NOTIFY changed)
    // A network request is running.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // "Updated 2026-10-20 14:05 - 12 sellers", "Shared list not updated (...)"
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // The report in progress: what is happening, and the outcome.
    Q_PROPERTY(bool reportBusy READ reportBusy NOTIFY changed)
    Q_PROPERTY(QString reportSellerId READ reportSellerId NOTIFY changed)
    Q_PROPERTY(QString reportStatus READ reportStatus NOTIFY changed)
    Q_PROPERTY(bool reportOk READ reportOk NOTIFY changed)
    // The purchase check is running (it can be stopped).
    Q_PROPERTY(bool purchaseCheckBusy READ purchaseCheckBusy NOTIFY changed)
    // The purchase check waits for the user in the browser (sign-in).
    Q_PROPERTY(QString purchaseMessage READ purchaseMessage NOTIFY changed)
    // A dispute's outcome.
    Q_PROPERTY(QString disputeStatus READ disputeStatus NOTIFY changed)
    Q_PROPERTY(bool disputeOk READ disputeOk NOTIFY changed)
    // Changes whenever the list changes (QML re-reads reportsFor()).
    Q_PROPERTY(int revision READ revision NOTIFY listChanged)

public:
    SharedListController(WorkFolder folder, std::function<bool()> lookupRunning, QObject *parent = nullptr);
    ~SharedListController() override;

    bool setUp() const { return m_client != nullptr; }
    bool readEnabled() const;
    bool reportEnabled() const;
    bool busy() const { return m_job != Job::None; }
    QString statusText() const;
    bool reportBusy() const { return m_reportBusy; }
    QString reportSellerId() const { return m_reportSellerId; }
    QString reportStatus() const { return m_reportStatus; }
    bool reportOk() const { return m_reportOk; }
    QString purchaseMessage() const { return m_purchase.userMessage(); }
    bool purchaseCheckBusy() const { return m_purchase.busy(); }
    QString disputeStatus() const { return m_disputeStatus; }
    bool disputeOk() const { return m_disputeOk; }
    int revision() const { return m_revision; }

    // At start-up: reads the saved list, and updates it when reading is on and
    // the saved one is older than a day.
    void startUp();

    Q_INVOKABLE void setReadEnabled(bool enabled);
    Q_INVOKABLE void setReportEnabled(bool enabled);
    Q_INVOKABLE void refresh();
    // [{id, reason, reasonLabel, text, month, itemChecked}] of the saved list.
    Q_INVOKABLE QVariantList reportsFor(const QString &sellerId) const;
    // Report lines by lower-case seller id (Results).
    QHash<QString, QStringList> linesBySeller() const;

    // Reports a seller: the purchase check, then the report.
    Q_INVOKABLE void report(const QString &sellerId, const QString &reason, const QString &text);
    Q_INVOKABLE void continuePurchaseCheck() { m_purchase.continueAfterUser(); }
    Q_INVOKABLE void cancelReport();
    Q_INVOKABLE void dispute(const QString &sellerId, const QString &reportId, const QString &text);

signals:
    void changed();
    void listChanged();

private:
    enum class Job { None, Fetch, Report, Dispute };

    void run(Job job);
    void jobDone();
    void onPurchaseFinished();
    void setReport(bool busy, const QString &status, bool ok);

    WorkFolder m_folder;
    std::function<bool()> m_lookupRunning;
    QNetworkAccessManager m_network;
    sellerlist::ListClient *m_client = nullptr;
    PurchaseCheck m_purchase;
    Job m_job = Job::None;
    QString m_lastError;
    bool m_reportBusy = false;
    QString m_reportSellerId;
    QString m_reportStatus;
    bool m_reportOk = false;
    std::optional<sellerlist::Report> m_report;
    std::optional<sellerlist::Dispute> m_dispute;
    QString m_disputeStatus;
    bool m_disputeOk = false;
    int m_revision = 0;
};

} // namespace app::ui
