#pragma once

#include "core/WorkFolder.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupStore.h"
#include "lookup/MatchWords.h"
#include "ui/LookupSession.h"
#include "ui/RecentModel.h"
#include "ui/ResultsController.h"
#include "ui/SettingsController.h"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace app::ui {

class PhotoProvider;

// Everything the QML views use (the app's logic is all in C++; QML is only
// the view, DESIGN 4.1): the lookup session, the results of the lookup
// shown, the recent lookups, the settings, and a few actions. Main.qml gets
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
    Q_PROPERTY(SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(QString pagesTodayText READ pagesTodayText NOTIFY pagesChanged)
    // Problems found at start that the user should know about (the user's
    // known-models file is damaged, the recent lookups cannot be read).
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString appName READ appName CONSTANT)
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
    SettingsController *settings() const { return m_settings; }
    QString pagesTodayText() const;
    QString notice() const { return m_notice; }
    QString appName() const;
    QString tagline() const;

    // Starts a lookup for a pasted link or item number.
    Q_INVOKABLE bool check(const QString &input);
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

signals:
    void pagesChanged();
    void noticeChanged();
    // A lookup is ready on the Results screen.
    void showResults();
    // openSaved() failed; `message` says why.
    void openFailed(const QString &message);

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
    SettingsController *m_settings;
};

} // namespace app::ui
