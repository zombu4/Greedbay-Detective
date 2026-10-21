#include "ui/SellerSession.h"

#include "lookup/Sellers.h"

#include <QDebug>
#include <QJsonObject>
#include <QUrl>

namespace app::ui {

SellerSession::SellerSession(WorkFolder folder, QString connectionName, QObject *parent)
    : QObject(parent), m_task(std::move(folder)), m_store(std::move(connectionName))
{
    connect(&m_task, &BrowserTask::changed, this, [this]() {
        m_pages = m_task.pagesUsed();
        emit changed();
    });
    connect(&m_task, &BrowserTask::finished, this, [this](bool answered, const QString &browserError) {
        m_pages = m_task.pagesUsed();
        if (m_task.cancelled()) {
            // What was read before the stop is kept, marked as a part.
            if (!m_stored && m_read && !m_read->reviews.isEmpty())
                store(QStringLiteral("stopped"));
            m_error = QStringLiteral("Stopped.");
        } else if (!answered && m_error.isEmpty()) {
            m_error = browserError.isEmpty() ? QStringLiteral("The browser closed before the reviews were read.")
                                             : QStringLiteral("The reviews were not read: %1").arg(browserError);
        }
        emit changed();
    });
}

bool SellerSession::read(const QString &sellerId, const QString &kind)
{
    m_error.clear();
    m_pages = 0;
    if (busy()) {
        m_error = QStringLiteral("The browser is busy; try again when it has finished.");
        emit changed();
        return false;
    }
    if (!lookup::isSellerId(sellerId) || (kind != QLatin1String("negative") && kind != QLatin1String("neutral"))) {
        m_error = QStringLiteral("The listing did not show the seller's eBay user id, so the reviews cannot be opened.");
        emit changed();
        return false;
    }
    m_sellerId = sellerId;
    m_kind = kind;
    m_read.reset();
    m_page = 0;
    m_stored = false;
    QString error;
    qInfo().noquote() << "sellers: reading the" << kind << "reviews of a seller";
    if (!m_task.start(QUrl(lookup::feedbackUrl(sellerId, kind)), ebay::PageKind::Feedback,
                      [this](const QJsonObject &reply) { return onReply(reply); }, &error)) {
        m_error = QStringLiteral("The reviews cannot be read: %1.").arg(error);
        emit changed();
        return false;
    }
    emit changed();
    return true;
}

std::optional<QString> SellerSession::onReply(const QJsonObject &reply)
{
    const QString status = reply.value(QStringLiteral("status")).toString();
    QString failure;
    if (status == QLatin1String("ok")) {
        const QJsonObject data = reply.value(QStringLiteral("result")).toObject();
        std::optional<lookup::SellerFeedback> part = lookup::SellerFeedback::fromJson(data);
        if (!part || part->sellerId.compare(m_sellerId, Qt::CaseInsensitive) != 0 || part->kind != m_kind) {
            failure = QStringLiteral("eBay's feedback page did not give the reviews in the expected form");
        } else {
            if (!m_read)
                m_read = std::move(part);
            else
                m_read->reviews.append(part->reviews);
            m_page = data.value(QStringLiteral("page")).toInt(m_page + 1);
            const int(&counts)[3] = m_kind == QLatin1String("negative") ? m_read->negative : m_read->neutral;
            const bool more = data.value(QStringLiteral("hasNext")).toBool() && m_read->reviews.size() < counts[2];
            if (more && m_page < kMaxPages)
                return QStringLiteral("%1@%2").arg(m_kind).arg(m_page + 1);
            store(more ? QStringLiteral("the app reads at most %1 pages of reviews").arg(kMaxPages) : QString());
            return std::nullopt;
        }
    } else if (status == QLatin1String("limit_reached")) {
        failure = QStringLiteral("today's eBay page limit is reached");
    } else if (status == QLatin1String("unexpected_page")) {
        failure = QStringLiteral("eBay did not show the expected list (%1)").arg(reply.value(QStringLiteral("reason")).toString());
    } else {
        failure = reply.value(QStringLiteral("error")).toString();
    }
    if (failure.isEmpty())
        failure = QStringLiteral("eBay's page gave no reviews (%1)").arg(status.isEmpty() ? QStringLiteral("no answer") : status);
    // A failure after some pages keeps what was read, marked as a part.
    if (m_read && !m_read->reviews.isEmpty())
        store(failure);
    else
        m_error = QStringLiteral("The reviews were not read: %1.").arg(failure);
    return std::nullopt;
}

void SellerSession::store(const QString &stopNote)
{
    m_stored = true;
    m_read->complete = stopNote.isEmpty();
    m_read->note = stopNote.isEmpty() ? QString()
                                      : QStringLiteral("Reading stopped after %1 of the list's pages: %2").arg(m_page).arg(stopNote);
    m_read->fetchedAt = QDateTime::currentDateTimeUtc();
    QString error;
    if (m_store.saveFeedback(*m_read, &error))
        emit loaded(m_sellerId, m_kind);
    else
        m_error = QStringLiteral("The reviews cannot be saved: %1").arg(error);
}

void SellerSession::cancel()
{
    m_task.cancel();
}

void SellerSession::clearError()
{
    if (m_error.isEmpty() && m_pages == 0)
        return;
    m_error.clear();
    m_pages = 0;
    emit changed();
}

} // namespace app::ui
