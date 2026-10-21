#pragma once

#include "core/WorkFolder.h"
#include "lookup/SellerStore.h"
#include "ui/BrowserTask.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class TestSellerPaging; // tests/tst_sellerpaging.cpp feeds recorded replies

namespace app::ui {

// Reads one seller's negative or neutral reviews of the last 12 months from
// eBay's classic feedback page (owner: "if clicked on the seller show all
// the negative reviews"), page by page as the list shows them. User-
// initiated only: one page load, then one paced, counted read on the page per
// further list page (BrowserTask); the result is stored in the database
// (SellerStore), also a part when the reading stopped early (with why).
class SellerSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the backend")
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool waitingForUser READ waitingForUser NOTIFY changed)
    // What the browser window needs from the user (sign in, a check page).
    Q_PROPERTY(QString userMessage READ userMessage NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // eBay pages the last read used (counted in the daily page cap).
    Q_PROPERTY(int pagesUsed READ pagesUsed NOTIFY changed)
    // The seller the last read is (or was) about.
    Q_PROPERTY(QString sellerId READ sellerId NOTIFY changed)

public:
    // The most list pages one read goes through (25 reviews a page).
    static constexpr int kMaxPages = 40;

    SellerSession(WorkFolder folder, QString connectionName, QObject *parent = nullptr);

    bool busy() const { return m_task.busy(); }
    bool waitingForUser() const { return !m_task.userMessage().isEmpty(); }
    QString userMessage() const { return m_task.userMessage(); }
    QString error() const { return m_error; }
    int pagesUsed() const { return m_pages; }
    QString sellerId() const { return m_sellerId; }

    // Starts reading; false (and error) when it cannot start.
    bool read(const QString &sellerId, const QString &kind);
    Q_INVOKABLE void continueAfterUser() { m_task.continueAfterUser(); }
    Q_INVOKABLE void cancel();
    // Forgets the last read's message and page count.
    void clearError();

signals:
    void changed();
    // The reviews are stored (SellerStore::feedback has them now).
    void loaded(const QString &sellerId, const QString &kind);

private:
    friend class ::TestSellerPaging;
    // One reply: keeps the page's reviews; the next page's option or done.
    std::optional<QString> onReply(const QJsonObject &reply);
    // Stores what was read (also a part, with why it stopped).
    void store(const QString &stopNote);

    BrowserTask m_task;
    lookup::SellerStore m_store;
    QString m_sellerId;
    QString m_kind;
    QString m_error;
    int m_pages = 0;
    // The reviews read so far and the list's page.
    std::optional<lookup::SellerFeedback> m_read;
    int m_page = 0;
    bool m_stored = false;
};

} // namespace app::ui
