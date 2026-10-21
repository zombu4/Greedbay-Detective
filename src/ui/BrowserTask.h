#pragma once

#include "core/WorkFolder.h"
#include "ebay/PageKind.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace app::browser {
class BrowserController;
}

namespace app::ui {

// One user-initiated task in the app's own browser session, outside a
// lookup: start the browser (the user's pacing and daily page cap), load one
// eBay page, then read again on that page as long as the task's step asks
// (BrowserController::readOnPage, paced and counted like loads), then close
// the browser. The seller reviews and the shared-report purchase check use
// it. Never runs while a lookup holds the browser (the callers check).
class BrowserTask : public QObject {
    Q_OBJECT

public:
    // After every reply: the option for the next read on the page, or
    // std::nullopt when the task is done (the browser is then closed).
    using Step = std::function<std::optional<QString>(const QJsonObject &reply)>;

    explicit BrowserTask(WorkFolder folder, QObject *parent = nullptr);

    // Starts; false and *error when it cannot.
    bool start(const QUrl &url, ebay::PageKind kind, Step step, QString *error);
    void continueAfterUser();
    // Stops after the current page; the step is not asked again.
    void cancel();

    bool busy() const { return m_controller != nullptr; }
    bool cancelled() const { return m_cancelled; }
    // What the browser window needs from the user (sign in, a check page).
    QString userMessage() const { return m_userMessage; }
    // eBay pages this task loaded or read again (counted in the cap).
    int pagesUsed() const { return m_pages; }

signals:
    void changed();
    // The browser has closed. answered: the step saw at least one reply;
    // browserError: why the browser session failed, if it did.
    void finished(bool answered, const QString &browserError);

private:
    void onDone(const QJsonObject &reply);
    void countPages();

    WorkFolder m_folder;
    QPointer<browser::BrowserController> m_controller;
    Step m_step;
    QString m_userMessage;
    QString m_browserError;
    int m_pages = 0;
    int m_usedBefore = 0;
    bool m_answered = false;
    bool m_cancelled = false;
};

} // namespace app::ui
