#include "ui/Backend.h"

#include "core/ExportFiles.h"
#include "core/PageBudget.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "ebay/SearchUrls.h"
#include "lookup/LookupExport.h"
#include "ui/PhotoProvider.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QUrl>

#include <utility>

namespace app::ui {

Backend::Backend(WorkFolder folder, const QString &connectionName, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_store(connectionName), m_photos(new PhotoProvider),
      m_results(nullptr), m_recent(new RecentModel(this)), m_history(nullptr), m_settings(nullptr)
{
    QString error;
    m_known = lookup::KnownModels::builtIn(&error);
    if (m_known && !m_known->addUserFile(m_folder.knownModelsPath(), &error)) {
        m_notice = QStringLiteral("Your saved brands and models (known-models.json in the work folder) are not used: %1").arg(error);
        error.clear();
    }
    m_words = lookup::MatchWords::builtIn(&error);
    if (!m_known || !m_words)
        m_fatalError = QStringLiteral("The app's matching data cannot be read: %1").arg(error);
    else
        m_session = new LookupSession(m_folder, &*m_known, &*m_words, &m_store, this);

    const QString settingsPath = m_folder.settingsPath();
    m_results = new ResultsController(
        m_store, m_words ? &*m_words : nullptr, [settingsPath]() { return Settings(settingsPath).valueSettings(); },
        [this](const QString &itemId) { return photoSourceOf(itemId); }, this);
    // The Home cards and the price history show the numbers stored with each
    // lookup.
    connect(m_results, &ResultsController::stored, this, &Backend::refreshRecent);
    m_history = new HistoryController(m_store, m_known && m_words ? &*m_known : nullptr, m_words ? &*m_words : nullptr, this);
    // "Re-check now": a new lookup of the item's newest listing.
    connect(m_history, &HistoryController::recheckRequested, this, [this](const QString &input) {
        if (!check(input, false))
            emit sessionFailed(m_session ? m_session->errorText() : m_fatalError);
    });
    m_settings = new SettingsController(m_folder, [this]() { return m_session && m_session->isRunning(); }, this);

    // Plugins: only the enabled ones run; matchers on each lookup before it
    // is saved, flaggers, valuators and exporters on the Results screen.
    // Plugin code and plugin .zip files run in a separate process: the app
    // itself with "--plugin-runner" (src/app/main.cpp).
    const plugins::RunnerCommand runner{QCoreApplication::applicationFilePath(), {QStringLiteral("--plugin-runner")}};
    m_pluginCatalog = std::make_unique<plugins::PluginCatalog>(m_folder, runner);
    m_pluginHost = std::make_unique<plugins::PluginHost>(
        *m_pluginCatalog, runner, [settingsPath]() { return Settings(settingsPath).pluginCallTimeLimitMs(); });
    m_pluginHost->reload();
    m_plugins = new PluginsController(m_folder, *m_pluginCatalog, *m_pluginHost, this);
    m_results->setPlugins(m_pluginHost.get());
    if (m_session)
        m_session->setPlugins(m_pluginHost.get());

    if (m_session) {
        connect(m_session, &LookupSession::saved, this, [this](qint64 lookupId) {
            refreshRecent();
            openSaved(lookupId);
        });
        connect(m_session, &LookupSession::photoSaved, this, [this](const QString &itemId) {
            if (m_results->hasLookup() && m_results->itemId() == itemId)
                m_results->photoChanged();
        });
        connect(m_session, &LookupSession::ended, this, &Backend::pagesChanged);
        // A failure after the start (the browser stopped, the save failed).
        connect(m_session, &LookupSession::errorChanged, this, [this]() {
            if (!m_session->errorText().isEmpty())
                emit sessionFailed(m_session->errorText());
        });
        connect(m_session, &LookupSession::progressChanged, this, &Backend::pagesChanged);
    }
    refreshRecent();
}

Backend::~Backend()
{
    // The session first, while the store and the database are still there:
    // it saves a lookup that is still running. Its signals no longer reach
    // this object.
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        delete m_session;
        m_session = nullptr;
    }
    if (!m_photosTaken)
        delete m_photos;
}

PhotoProvider *Backend::takePhotoProvider()
{
    m_photosTaken = true;
    return m_photos;
}

void Backend::refreshRecent()
{
    QString error;
    if (!m_recent->refresh(m_store, m_words ? &*m_words : nullptr, &error)) {
        m_notice = QStringLiteral("The recent lookups cannot be read: %1").arg(error);
        emit noticeChanged();
    }
    m_history->refresh();
}

QString Backend::pagesTodayText() const
{
    const PageBudget budget(m_folder.pageCountPath(), Settings(m_folder.settingsPath()).dailyPageCap());
    QString error;
    const int used = budget.usedToday(&error);
    if (used < 0)
        return QStringLiteral("eBay pages used today: unknown (%1)").arg(error);
    return QStringLiteral("eBay pages used today: %1 of %2").arg(used).arg(budget.dailyCap());
}

QString Backend::appName() const
{
    return QString::fromLatin1(version::kDisplayName);
}

QString Backend::uninstallNote() const
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("The app closes, removes your data as you choose, moves itself to the Trash and shows what was "
                          "done.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("The app closes and starts its uninstaller, which asks the same question, removes your data as "
                          "you choose and removes the program.");
#else
    return QStringLiteral("The app closes, removes your data as you choose, then removes its package (it asks for your "
                          "password) and shows what was done.");
#endif
}

QString Backend::uninstall(bool keepHistory)
{
    if (m_session && m_session->isRunning())
        return QStringLiteral("A lookup is running: uninstall after it has finished.");
#if defined(Q_OS_WIN)
    Q_UNUSED(keepHistory);
    // The installer puts the program in <install>\bin and its uninstaller in
    // <install> (packaging/windows/NSIS.template.in).
    const QString uninstaller = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../Uninstall.exe"));
    if (!QFileInfo(uninstaller).isFile())
        return QStringLiteral("There is no uninstaller next to this copy of the app (%1).").arg(QDir::toNativeSeparators(uninstaller));
    if (!QProcess::startDetached(uninstaller, {}))
        return QStringLiteral("The uninstaller cannot be started (%1).").arg(QDir::toNativeSeparators(uninstaller));
#else
    m_uninstallKeep = keepHistory;
#endif
    QCoreApplication::quit();
    return {};
}

QString Backend::tagline() const
{
    return QStringLiteral("fight greedy garbage sellers with knowledge");
}

bool Backend::check(const QString &input, bool reviewFirst)
{
    if (!m_session)
        return false;
    return m_session->start(input, reviewFirst);
}

bool Backend::openSaved(qint64 lookupId)
{
    QString error;
    if (!m_results->show(lookupId, &error)) {
        emit openFailed(QStringLiteral("This lookup cannot be shown: %1").arg(error));
        return false;
    }
    emit showResults();
    return true;
}

bool Backend::correctIdentity(const QString &brand, const QString &model, const QString &productName)
{
    if (!m_session || !m_results->hasLookup())
        return false;
    return m_session->startCorrected(m_results->saved(), brand, model, productName);
}

void Backend::openOnEbay(const QString &itemId) const
{
    // Only an item number (digits) becomes an address, and only eBay's.
    if (ebay::itemIdFromInput(itemId) != itemId)
        return;
    QDesktopServices::openUrl(ebay::itemUrl(itemId));
}

QString Backend::listingFromText(const QString &text) const
{
    const QString trimmed = text.trimmed();
    return ebay::itemIdFromInput(trimmed).isEmpty() ? QString() : trimmed;
}

QString Backend::clipboardText() const
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}

QVariantMap Backend::exportData()
{
    auto reply = [](bool ok, const QString &message, const QString &path = QString()) {
        return QVariantMap{{QStringLiteral("ok"), ok}, {QStringLiteral("message"), message}, {QStringLiteral("path"), path}};
    };
    QString error;
    const std::optional<QList<lookup::LookupSummary>> summaries = m_store.recent(0, &error);
    if (!summaries)
        return reply(false, QStringLiteral("The saved lookups cannot be read: %1").arg(error));
    QList<lookup::SavedLookup> lookups;
    for (const lookup::LookupSummary &summary : *summaries) {
        std::optional<lookup::SavedLookup> lookup = m_store.load(summary.id, &error);
        if (!lookup)
            return reply(false, QStringLiteral("Lookup %1 cannot be read: %2").arg(summary.id).arg(error));
        lookups.append(std::move(*lookup));
    }
    const QString stem = QStringLiteral("%1-lookups-%2")
                             .arg(QString::fromLatin1(version::kPackageName),
                                  QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString folder = exportFolder();
    const std::optional<QString> csv = saveExport(folder, stem + QStringLiteral(".csv"), lookup::lookupsCsv(*summaries), &error);
    if (!csv)
        return reply(false, QStringLiteral("Not saved: %1").arg(error));
    const std::optional<QString> json = saveExport(folder, stem + QStringLiteral(".json"), lookup::lookupsJson(lookups), &error);
    if (!json)
        return reply(false, QStringLiteral("Saved %1, but not the JSON file: %2").arg(QDir::toNativeSeparators(*csv), error), *csv);
    return reply(true,
                 QStringLiteral("Saved %1 as %2 and %3")
                     .arg(summaries->size() == 1 ? QStringLiteral("1 lookup") : QStringLiteral("%1 lookups").arg(summaries->size()),
                          QDir::toNativeSeparators(*csv), QDir::toNativeSeparators(*json)),
                 *csv);
}

void Backend::showFolderOf(const QString &path) const
{
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

QString Backend::photoSourceOf(const QString &itemId)
{
    if (itemId.isEmpty())
        return {};
    const QImage image = QImage::fromData(m_store.photo(itemId));
    if (image.isNull())
        return {};
    m_photos->setPhoto(itemId, image);
    return QStringLiteral("image://listing/%1?%2").arg(itemId).arg(++m_photoVersion);
}

} // namespace app::ui
