#include "ui/BrowserTask.h"

#include "browser/BrowserCatalog.h"
#include "browser/BrowserController.h"
#include "core/PageBudget.h"
#include "core/Settings.h"

#include <algorithm>

namespace app::ui {

BrowserTask::BrowserTask(WorkFolder folder, QObject *parent) : QObject(parent), m_folder(std::move(folder)) {}

bool BrowserTask::start(const QUrl &url, ebay::PageKind kind, Step step, QString *error)
{
    if (busy()) {
        *error = QStringLiteral("the browser is busy with another task");
        return false;
    }
    const Settings settings(m_folder.settingsPath());
    const std::optional<browser::InstalledBrowser> chosen =
        browser::BrowserCatalog::choose(browser::BrowserCatalog::installed(), settings.preferredBrowser());
    if (!chosen) {
        *error = QStringLiteral("no supported browser is installed");
        return false;
    }
    browser::BrowserControllerOptions options;
    options.pageDelayMinMs = settings.pageDelayMinMs();
    options.pageDelayMaxMs = settings.pageDelayMaxMs();
    options.dailyPageCap = settings.dailyPageCap();
    auto *controller = new browser::BrowserController(m_folder, *chosen, options, this);
    if (!controller->start(error)) {
        delete controller;
        *error = QStringLiteral("%1 could not be started: %2").arg(chosen->name, *error);
        return false;
    }
    m_controller = controller;
    m_step = std::move(step);
    m_userMessage.clear();
    m_browserError.clear();
    m_pages = 0;
    m_answered = false;
    m_cancelled = false;
    m_usedBefore = PageBudget(m_folder.pageCountPath(), settings.dailyPageCap()).usedToday();
    connect(controller, &browser::BrowserController::ready, this, [this, url, kind]() {
        if (m_controller)
            m_controller->fetch(url, kind);
    });
    connect(controller, &browser::BrowserController::done, this, &BrowserTask::onDone);
    connect(controller, &browser::BrowserController::needsUser, this, [this](const QString &message) {
        m_userMessage = message;
        emit changed();
    });
    connect(controller, &browser::BrowserController::failed, this, [this](const QString &message) { m_browserError = message; });
    connect(controller, &browser::BrowserController::finished, this, [this](bool) {
        countPages();
        if (m_controller)
            m_controller->deleteLater();
        m_controller = nullptr;
        m_userMessage.clear();
        emit changed();
        emit finished(m_answered, m_browserError);
    });
    emit changed();
    return true;
}

void BrowserTask::onDone(const QJsonObject &reply)
{
    m_userMessage.clear();
    countPages();
    if (m_cancelled) {
        emit changed();
        if (m_controller)
            m_controller->stop();
        return;
    }
    m_answered = true;
    m_nextUrl.clear();
    const std::optional<QString> next = m_step(reply);
    emit changed();
    if (!m_nextUrl.isEmpty() && m_controller && !m_cancelled)
        m_controller->fetch(m_nextUrl, m_nextKind);
    else if (next && m_controller && !m_cancelled)
        m_controller->readOnPage(*next);
    else if (m_controller)
        m_controller->stop();
}

void BrowserTask::countPages()
{
    // Every page the cap counted, also one whose reading failed or was
    // stopped after the load.
    const PageBudget budget(m_folder.pageCountPath(), Settings(m_folder.settingsPath()).dailyPageCap());
    m_pages = std::max(0, budget.usedToday() - m_usedBefore);
}

void BrowserTask::continueAfterUser()
{
    if (m_controller)
        m_controller->continueAfterUser();
}

void BrowserTask::cancel()
{
    if (!m_controller)
        return;
    m_cancelled = true;
    emit changed();
    m_controller->stop();
}

} // namespace app::ui
