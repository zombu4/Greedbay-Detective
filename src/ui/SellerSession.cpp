#include "ui/SellerSession.h"

#include "browser/BrowserCatalog.h"
#include "browser/BrowserController.h"
#include "core/PageBudget.h"
#include "core/Settings.h"
#include "lookup/Sellers.h"

#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace app::ui {

SellerSession::SellerSession(WorkFolder folder, QString connectionName, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_store(std::move(connectionName))
{
}

bool SellerSession::read(const QString &sellerId, const QString &kind)
{
    m_error.clear();
    m_pages = 0;
    if (busy()) {
        m_error = QStringLiteral("The reviews of a seller are being read already.");
        emit changed();
        return false;
    }
    if (!lookup::isSellerId(sellerId) || (kind != QLatin1String("negative") && kind != QLatin1String("neutral"))) {
        m_error = QStringLiteral("The listing did not show the seller's eBay user id, so the reviews cannot be opened.");
        emit changed();
        return false;
    }
    const Settings settings(m_folder.settingsPath());
    const std::optional<browser::InstalledBrowser> chosen =
        browser::BrowserCatalog::choose(browser::BrowserCatalog::installed(), settings.preferredBrowser());
    if (!chosen) {
        m_error = QStringLiteral("No supported browser is installed.");
        emit changed();
        return false;
    }
    browser::BrowserControllerOptions options;
    options.pageDelayMinMs = settings.pageDelayMinMs();
    options.pageDelayMaxMs = settings.pageDelayMaxMs();
    options.dailyPageCap = settings.dailyPageCap();
    auto *controller = new browser::BrowserController(m_folder, *chosen, options, this);
    QString error;
    if (!controller->start(&error)) {
        delete controller;
        m_error = QStringLiteral("%1 could not be started: %2").arg(chosen->name, error);
        emit changed();
        return false;
    }
    m_controller = controller;
    m_sellerId = sellerId;
    m_kind = kind;
    m_userMessage.clear();
    m_browserError.clear();
    m_answered = false;
    m_cancelled = false;
    m_usedBefore = PageBudget(m_folder.pageCountPath(), settings.dailyPageCap()).usedToday();
    connect(controller, &browser::BrowserController::ready, this, [this]() {
        qInfo().noquote() << "sellers: reading the" << m_kind << "reviews of a seller";
        m_controller->fetch(QUrl(lookup::feedbackUrl(m_sellerId, m_kind)), ebay::PageKind::Feedback);
    });
    connect(controller, &browser::BrowserController::done, this, &SellerSession::onDone);
    connect(controller, &browser::BrowserController::needsUser, this, [this](const QString &message) {
        m_userMessage = message;
        emit changed();
    });
    connect(controller, &browser::BrowserController::failed, this, [this](const QString &message) { m_browserError = message; });
    connect(controller, &browser::BrowserController::finished, this, [this](bool) {
        if (!m_answered && m_error.isEmpty())
            m_error = m_browserError.isEmpty() ? QStringLiteral("The browser closed before the reviews were read.")
                                               : QStringLiteral("The reviews were not read: %1").arg(m_browserError);
        if (m_controller)
            m_controller->deleteLater();
        m_controller = nullptr;
        m_userMessage.clear();
        emit changed();
    });
    emit changed();
    return true;
}

void SellerSession::onDone(const QJsonObject &reply)
{
    m_answered = true;
    m_userMessage.clear();
    // Every page the cap counted, also one whose reading failed or was
    // stopped after the load.
    const PageBudget budget(m_folder.pageCountPath(), Settings(m_folder.settingsPath()).dailyPageCap());
    m_pages = std::max(0, budget.usedToday() - m_usedBefore);
    const QString status = reply.value(QStringLiteral("status")).toString();
    if (m_cancelled) {
        // "Stopped." stays the message.
    } else if (status == QLatin1String("ok")) {
        std::optional<lookup::SellerFeedback> feedback =
            lookup::SellerFeedback::fromJson(reply.value(QStringLiteral("result")).toObject());
        QString error;
        if (!feedback || feedback->sellerId.compare(m_sellerId, Qt::CaseInsensitive) != 0 || feedback->kind != m_kind) {
            m_error = QStringLiteral("eBay's feedback page did not give the reviews in the expected form.");
        } else {
            feedback->fetchedAt = QDateTime::currentDateTimeUtc();
            if (m_store.saveFeedback(*feedback, &error))
                emit loaded(m_sellerId, m_kind);
            else
                m_error = QStringLiteral("The reviews cannot be saved: %1").arg(error);
        }
    } else if (status == QLatin1String("limit_reached")) {
        m_error = QStringLiteral("Today's eBay page limit is reached; the reviews were not read.");
    } else if (status == QLatin1String("unexpected_page")) {
        m_error = QStringLiteral("eBay did not show the seller's feedback page (%1).").arg(reply.value(QStringLiteral("reason")).toString());
    } else {
        m_error = QStringLiteral("The reviews were not read: %1").arg(reply.value(QStringLiteral("error")).toString());
    }
    emit changed();
    close();
}

void SellerSession::clearError()
{
    if (m_error.isEmpty() && m_pages == 0)
        return;
    m_error.clear();
    m_pages = 0;
    emit changed();
}

void SellerSession::continueAfterUser()
{
    if (m_controller)
        m_controller->continueAfterUser();
}

void SellerSession::cancel()
{
    if (!m_controller)
        return;
    m_answered = true;
    m_cancelled = true;
    m_error = QStringLiteral("Stopped.");
    emit changed();
    close();
}

void SellerSession::close()
{
    if (m_controller)
        m_controller->stop();
}

} // namespace app::ui
