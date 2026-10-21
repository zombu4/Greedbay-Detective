#pragma once

#include "core/WorkFolder.h"
#include "ui/BrowserTask.h"

#include <QObject>
#include <QString>

#include <optional>

class TestSellerPaging; // tests/tst_sellerpaging.cpp feeds recorded replies

namespace app::ui {

// The shared-report purchase check (owner decision 2026-10-20: "the app
// checks the reporter's purchase history so only real purchases can be
// reported"): reads the user's own eBay Purchases page in the app's browser
// session, page by page (paced, counted in the daily cap), until it finds an
// order from the seller. Only that order's item number and month are kept;
// nothing else of the page is read (purchases.js) or leaves this object.
class PurchaseCheck : public QObject {
    Q_OBJECT

public:
    // The most pages of purchases looked through (25 orders a page).
    static constexpr int kMaxPages = 30;

    struct Found {
        QString itemId;
        QString month; // "2026-10"
    };

    explicit PurchaseCheck(WorkFolder folder, QObject *parent = nullptr);

    // Starts; false and error() when it cannot.
    bool check(const QString &sellerId);
    void continueAfterUser() { m_task.continueAfterUser(); }
    void cancel() { m_task.cancel(); }

    bool busy() const { return m_task.busy(); }
    QString userMessage() const { return m_task.userMessage(); }
    int pagesUsed() const { return m_task.pagesUsed(); }
    QString error() const { return m_error; }
    // After finished(): the order, when one was found.
    std::optional<Found> found() const { return m_found; }

signals:
    void changed();
    // The check is over: found() has the order, or error() says why not
    // (also "no purchase from this seller in the last N orders").
    void finished();

private:
    friend class ::TestSellerPaging;
    std::optional<QString> onReply(const QJsonObject &reply);

    BrowserTask m_task;
    QString m_sellerId;
    QString m_error;
    std::optional<Found> m_found;
    int m_page = 0;
    int m_orders = 0;
};

} // namespace app::ui
