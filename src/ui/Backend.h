#pragma once

#include "core/WorkFolder.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupStore.h"
#include "lookup/MatchWords.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginHost.h"
#include "ui/HistoryController.h"
#include "ui/LookupSession.h"
#include "ui/PluginsController.h"
#include "ui/RecentModel.h"
#include "ui/ResultsController.h"
#include "ui/SellerController.h"
#include "ui/SettingsController.h"

#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace app::ui {

class PhotoProvider;

// Everything the QML views use (the app's logic is all in C++; QML is only
// the view, DESIGN 4.1): the lookup session, the results of the lookup
// shown, the recent lookups, the price history, the settings and plugins,
// and a few actions. Main.qml gets
// it as its required property "backend".
//
// The database connection `connectionName` must be open (db/Database) for
// the Backend's whole life.
class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    Q_PROPERTY(LookupSession *session READ session CONSTANT)
    Q_PROPERTY(ResultsController *results READ results CONSTANT)
    Q_PROPERTY(RecentModel *recent READ recent CONSTANT)
    Q_PROPERTY(HistoryController *history READ history CONSTANT)
    Q_PROPERTY(PluginsController *plugins READ plugins CONSTANT)
    Q_PROPERTY(SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(SellerController *sellers READ sellers CONSTANT)
    Q_PROPERTY(QString pagesTodayText READ pagesTodayText NOTIFY pagesChanged)
    // Problems found at start that the user should know about (the user's
    // known-models file is damaged, the recent lookups cannot be read).
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    // What "Uninstall..." does with the program on this system, in plain
    // words (the Settings screen shows it before the user decides).
    Q_PROPERTY(QString uninstallNote READ uninstallNote CONSTANT)
    Q_PROPERTY(QString tagline READ tagline CONSTANT)
public:
    Backend(WorkFolder folder, const QString &connectionName, QObject *parent = nullptr);
    ~Backend() override;

    // Empty when the Backend can run lookups; else why not (the built-in
    // matching data cannot be read: the build is broken).
    QString fatalError() const { return m_fatalError; }

    // For QQmlEngine::addImageProvider("listing", ...); the engine takes
    // ownership. Created once.
    PhotoProvider *takePhotoProvider();

    LookupSession *session() const { return m_session; }
    ResultsController *results() const { return m_results; }
    RecentModel *recent() const { return m_recent; }
    HistoryController *history() const { return m_history; }
    PluginsController *plugins() const { return m_plugins; }
    SettingsController *settings() const { return m_settings; }
    SellerController *sellers() const { return m_sellers; }
    QString pagesTodayText() const;
    QString notice() const { return m_notice; }
    QString appName() const;
    QString uninstallNote() const;
    QString tagline() const;

    // Starts a lookup for a pasted link or item number; `reviewFirst`: let
    // the user check the searches and depths before they run.
    Q_INVOKABLE bool check(const QString &input, bool reviewFirst = false);
    // Shows a saved lookup on the Results screen (showResults()).
    Q_INVOKABLE bool openSaved(qint64 lookupId);
    // "Wrong item? Correct it": saves the identity for the shown listing and
    // searches again; the new lookup is saved and shown.
    Q_INVOKABLE bool correctIdentity(const QString &brand, const QString &model, const QString &productName);
    // Opens a listing in the system's default browser (not the app's
    // browser instance).
    Q_INVOKABLE void openOnEbay(const QString &itemId) const;
    // The text when it is an eBay listing link or item number (pasted or
    // dropped anywhere in the window), else an empty string.
    Q_INVOKABLE QString listingFromText(const QString &text) const;
    // The clipboard's text (for a paste outside a text field).
    Q_INVOKABLE QString clipboardText() const;
    // "Export data" (DESIGN 8): every saved lookup as CSV (one line each)
    // and as JSON (with every row), saved in the Downloads folder.
    // {ok, message, path}: `path` is the CSV file's.
    Q_INVOKABLE QVariantMap exportData();
    // Shows the folder holding `path` in the system's file manager.
    Q_INVOKABLE void showFolderOf(const QString &path) const;

    // "Uninstall..." (PLAN 9, owner decision: full uninstall with a prompt
    // to keep saved history). Refused while a lookup runs (the reason is
    // returned). macOS and Linux: records the choice and quits; main() then
    // removes the data (core/Uninstall) once the database, the log and the
    // lock are closed, removes the program and shows what was done. Windows:
    // starts the installer's uninstaller, which asks the same question and
    // removes the data and the program, and quits. Empty when it went ahead.
    Q_INVOKABLE QString uninstall(bool keepHistory);
    // The choice uninstall() recorded (macOS and Linux), if any.
    std::optional<bool> uninstallRequest() const { return m_uninstallKeep; }

signals:
    void pagesChanged();
    void noticeChanged();
    // A lookup is ready on the Results screen.
    void showResults();
    // openSaved() failed; `message` says why.
    void openFailed(const QString &message);
    // The lookup session reported a problem (also after it started).
    void sessionFailed(const QString &message);

private:
    QString photoSourceOf(const QString &itemId);
    void refreshRecent();

    WorkFolder m_folder;
    std::optional<lookup::KnownModels> m_known;
    std::optional<lookup::MatchWords> m_words;
    lookup::LookupStore m_store;
    PhotoProvider *m_photos;
    bool m_photosTaken = false;
    quint64 m_photoVersion = 0;
    QString m_fatalError;
    QString m_notice;
    LookupSession *m_session = nullptr;
    ResultsController *m_results;
    RecentModel *m_recent;
    HistoryController *m_history;
    // Plugins: the catalog, then the host that runs the enabled ones.
    std::unique_ptr<plugins::PluginCatalog> m_pluginCatalog;
    std::unique_ptr<plugins::PluginHost> m_pluginHost;
    PluginsController *m_plugins = nullptr;
    std::optional<bool> m_uninstallKeep;
    SettingsController *m_settings;
    SellerController *m_sellers;
    QString m_connectionName;
};

} // namespace app::ui
