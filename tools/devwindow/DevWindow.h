#pragma once

#include "browser/BrowserCatalog.h"
#include "core/WorkFolder.h"

#include <QJsonObject>
#include <QWidget>

#include <optional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace app::browser {
class BrowserController;
}

// Developer window for checking the browser session by hand. Each button runs
// one session in the user's installed browser (chosen by the preference order
// or the "browser/preferred" setting), with its own profile folder in the
// work folder:
//   Sign in to eBay   opens the eBay homepage in the browser; the user clicks
//                     "Sign in" there himself, signs in, then presses Continue
//   Continue          the user has done what the message above asks for
//   Fetch test page   fetches the URL in the field as an active search and
//                     shows the extracted JSON
//   Sign out          deletes this browser's profile folder in the work folder
//                     (only while the app's browser is closed)
// Every step is written to the text area, including the browser's process
// id and how it exited.
class DevWindow : public QWidget {
    Q_OBJECT
public:
    explicit DevWindow(app::WorkFolder folder, QWidget *parent = nullptr);
    ~DevWindow() override;

private:
    enum class Task { SignIn, Fetch };

    void runSession(Task task);
    void onReady();
    void onDone(const QJsonObject &reply);
    void onFinished(bool normal);
    void signOut();
    void setBusy(bool busy);
    void append(const QString &line);
    void refreshBrowser();

    app::WorkFolder m_folder;
    std::optional<app::browser::InstalledBrowser> m_browser;
    app::browser::BrowserController *m_controller = nullptr;
    Task m_task = Task::Fetch;
    QLineEdit *m_url;
    QLabel *m_browserLabel;
    QPushButton *m_signIn;
    QPushButton *m_continue;
    QPushButton *m_fetch;
    QPushButton *m_signOut;
    QLabel *m_status;
    QPlainTextEdit *m_output;
};
