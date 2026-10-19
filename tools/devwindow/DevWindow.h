#pragma once

#include "browser/BrowserCatalog.h"
#include "core/WorkFolder.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupReport.h"
#include "lookup/MatchWords.h"

#include <QJsonObject>
#include <QWidget>

#include <optional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace app::browser {
class BrowserController;
}
namespace app::lookup {
class LookupRunner;
}

// Developer window for checking the browser session and a lookup by hand
// (not the app's user interface). Each action runs one session in the
// user's installed browser (chosen by the preference order or the
// "browser/preferred" setting), with its own profile folder in the work
// folder:
//   Sign in to eBay   opens the eBay homepage in the browser; the user clicks
//                     "Sign in" there, signs in, then presses Continue
//   Continue          the user has done what the message above asks for
//   Fetch test page   fetches the URL in the field as an active search and
//                     shows the extracted JSON
//   Sign out          deletes this browser's profile folder in the work folder
//                     (only while the app's browser is closed)
//   Check listing     reads the pasted listing and shows its identity (brand,
//                     model, product name; editable) and the queries built
//                     from it; when the model is not known with confidence
//                     it asks for it and searches nothing until it is given
//   Save identity     saves the identity for this listing and adds it to the
//                     known models in the work folder; the queries are built
//                     anew
//   Fetch comps       reads the sold search, the active search and Product
//                     Research for the queries (one per line) up to the
//                     depths shown, then lists every row by bucket with a
//                     checkbox (ticked = counted; same-item rows start
//                     ticked) and its reasons; the numbers below recompute
//                     when a box changes
//   Cancel            stops the lookup after the page being loaded
// Every step is written to the log area, including the browser's process
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
    void showIdentity();
    void saveIdentity();
    void fetchComps();
    void cancelLookup();
    void showRows();
    void recompute();
    void signOut();
    void setBusy(bool busy);
    void append(const QString &line);
    void refreshBrowser();
    QString pagesToday() const;

    app::WorkFolder m_folder;
    std::optional<app::lookup::KnownModels> m_known;
    std::optional<app::lookup::MatchWords> m_words;
    std::optional<app::browser::InstalledBrowser> m_browser;
    app::browser::BrowserController *m_controller = nullptr;
    app::lookup::LookupRunner *m_runner = nullptr;
    app::lookup::LookupResult m_result;
    QList<app::lookup::Row> m_rows;
    // Tree item -> index into m_rows.
    QHash<QTreeWidgetItem *, qsizetype> m_rowOf;
    Task m_task = Task::Fetch;
    bool m_waitingForUser = false;
    bool m_filling = false;
    QLineEdit *m_url;
    QLabel *m_browserLabel;
    QPushButton *m_signIn;
    QPushButton *m_continue;
    QPushButton *m_fetch;
    QPushButton *m_signOut;
    QLineEdit *m_listing;
    QPushButton *m_check;
    QLineEdit *m_brand;
    QLineEdit *m_model;
    QLineEdit *m_productName;
    QPushButton *m_saveIdentity;
    QLabel *m_identityNote;
    QPlainTextEdit *m_queries;
    QLineEdit *m_expectation;
    QSpinBox *m_soldDepth;
    QSpinBox *m_activeDepth;
    QSpinBox *m_researchDepth;
    QPushButton *m_fetchComps;
    QPushButton *m_cancel;
    QLabel *m_status;
    QTreeWidget *m_table;
    QPlainTextEdit *m_numbers;
    QPlainTextEdit *m_output;
};
