#include "ui/Backend.h"

#include "core/PageBudget.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "ebay/SearchUrls.h"
#include "ui/PhotoProvider.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImage>

#include <utility>

namespace app::ui {

Backend::Backend(WorkFolder folder, const QString &connectionName, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_store(connectionName), m_photos(new PhotoProvider),
      m_results(nullptr), m_recent(new RecentModel(this)), m_settings(nullptr)
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
        m_store, [settingsPath]() { return Settings(settingsPath).valueSettings(); },
        [this](const QString &itemId) { return photoSourceOf(itemId); }, this);
    // The Home cards show the numbers stored with each lookup.
    connect(m_results, &ResultsController::stored, this, &Backend::refreshRecent);
    m_settings = new SettingsController(m_folder, [this]() { return m_session && m_session->isRunning(); }, this);

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
    if (!m_recent->refresh(m_store, &error)) {
        m_notice = QStringLiteral("The recent lookups cannot be read: %1").arg(error);
        emit noticeChanged();
    }
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
