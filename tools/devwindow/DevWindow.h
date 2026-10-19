#pragma once

#include "browser/BrowserCatalog.h"
#include "core/WorkFolder.h"

#include <QJsonObject>
#include <QWidget>

#include <array>
#include <optional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace app::browser {
class BrowserController;
}
namespace app::lookup {
class LookupRunner;
}

// Developer window for checking the browser session and a rough lookup by
// hand (not the app's user interface). Each action runs one session in the
// user's installed browser (chosen by the preference order or the
// "browser/preferred" setting), with its own profile folder in the work
// folder:
//   Sign in to eBay   opens the eBay homepage in the browser; the user clicks
//                     "Sign in" there himself, signs in, then presses Continue
//   Continue          the user has done what the message above asks for
//   Fetch test page   fetches the URL in the field as an active search and
//                     shows the extracted JSON
//   Sign out          deletes this browser's profile folder in the work folder
//                     (only while the app's browser is closed)
//   Check listing     reads the pasted listing, shows 1-3 search queries to
//                     edit, then "Fetch comps" reads the sold search, the
//                     active search and Product Research up to the depths
//                     shown (defaults from Settings) and prints text tables
//                     with the median and P25-P75 of confirmed sold totals
//   Cancel            stops the lookup after the page being loaded
// Every step is written to the text area, including the browser's process
// id, how it exited and how many eBay pages were loaded today (daily cap).
class DevWindow : public QWidget {
    Q_OBJECT
public:
    explicit DevWindow(app::WorkFolder folder, QWidget *parent = nullptr);
    ~DevWindow() override;

private:
    enum class Task { SignIn, Fetch, Lookup };

    void runSession(Task task);
    void onReady();
    void onDone(const QJsonObject &reply);
    void onFinished(bool normal);
    void checkListing();
    void fetchComps();
    void cancelLookup();
    void signOut();
    void setBusy(bool busy);
    void append(const QString &line);
    void refreshBrowser();
    QString pagesToday() const;

    app::WorkFolder m_folder;
    std::optional<app::browser::InstalledBrowser> m_browser;
    app::browser::BrowserController *m_controller = nullptr;
    app::lookup::LookupRunner *m_runner = nullptr;
    Task m_task = Task::Fetch;
    bool m_waitingForUser = false;
    QLineEdit *m_url;
    QLabel *m_browserLabel;
    QPushButton *m_signIn;
    QPushButton *m_continue;
    QPushButton *m_fetch;
    QPushButton *m_signOut;
    QLineEdit *m_listing;
    QPushButton *m_check;
    std::array<QLineEdit *, 3> m_queries{};
    QSpinBox *m_soldDepth;
    QSpinBox *m_activeDepth;
    QSpinBox *m_researchDepth;
    QPushButton *m_fetchComps;
    QPushButton *m_cancel;
    QLabel *m_status;
    QPlainTextEdit *m_output;
};
