#pragma once

#include "core/WorkFolder.h"
#include "lookup/SellerStore.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace app::browser {
class BrowserController;
}

namespace app::ui {

// Reads one seller's negative or neutral reviews from eBay's classic
// feedback page (owner: "if clicked on the seller show all the negative
// reviews"). User-initiated only: one page load in the app's own browser
// session, with the same pacing and daily page cap as a lookup; the result
// is stored in the database (SellerStore) and the browser is closed again.
// Never runs while a lookup holds the browser (the caller checks).
class SellerSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the backend")
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool waitingForUser READ waitingForUser NOTIFY changed)
    // What the browser window needs from the user (sign in, a check page).
    Q_PROPERTY(QString userMessage READ userMessage NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // eBay pages this session loaded (0 or 1).
    Q_PROPERTY(int pagesUsed READ pagesUsed NOTIFY changed)

public:
    SellerSession(WorkFolder folder, QString connectionName, QObject *parent = nullptr);

    bool busy() const { return m_controller != nullptr; }
    bool waitingForUser() const { return !m_userMessage.isEmpty(); }
    QString userMessage() const { return m_userMessage; }
    QString error() const { return m_error; }
    int pagesUsed() const { return m_pages; }

    // Starts reading; false (and error) when it cannot start.
    bool read(const QString &sellerId, const QString &kind);
    Q_INVOKABLE void continueAfterUser();
    void clearError();
    Q_INVOKABLE void cancel();

signals:
    void changed();
    // The reviews are stored (SellerStore::feedback has them now).
    void loaded(const QString &sellerId, const QString &kind);

private:
    void onDone(const QJsonObject &reply);
    void close();

    WorkFolder m_folder;
    lookup::SellerStore m_store;
    QPointer<browser::BrowserController> m_controller;
    QString m_sellerId;
    QString m_kind;
    QString m_userMessage;
    QString m_error;
    QString m_browserError;
    int m_pages = 0;
    bool m_answered = false;
};

} // namespace app::ui
