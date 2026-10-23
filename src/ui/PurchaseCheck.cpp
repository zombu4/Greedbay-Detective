#include "ui/PurchaseCheck.h"

#include "ebay/SearchUrls.h"
#include "lookup/Records.h"

#include "lookup/Sellers.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace app::ui {

namespace {

// The Purchases page (recon 2026-10-20, "the purchases page").
const QUrl kPurchasesUrl(QStringLiteral("https://www.ebay.com/mye/myebay/purchase"));

} // namespace

PurchaseCheck::PurchaseCheck(WorkFolder folder, QObject *parent) : QObject(parent), m_task(std::move(folder))
{
    connect(&m_task, &BrowserTask::changed, this, &PurchaseCheck::changed);
    connect(&m_task, &BrowserTask::finished, this, [this](bool answered, const QString &browserError) {
        if (m_task.cancelled())
            m_error = QStringLiteral("Stopped.");
        else if (!answered && m_error.isEmpty())
            m_error = browserError.isEmpty() ? QStringLiteral("The browser closed before your purchases were read.")
                                             : QStringLiteral("Your purchases were not read: %1").arg(browserError);
        emit changed();
        emit finished();
    });
}

bool PurchaseCheck::check(const QString &sellerId)
{
    m_error.clear();
    m_found.reset();
    m_page = 0;
    m_orders = 0;
    m_checkingItem = false;
    if (!lookup::isSellerId(sellerId)) {
        m_error = QStringLiteral("There is no eBay user id of this seller to look for.");
        emit changed();
        return false;
    }
    m_sellerId = sellerId;
    QString error;
    if (!m_task.start(kPurchasesUrl, ebay::PageKind::Purchases, [this](const QJsonObject &reply) { return onReply(reply); },
                      &error)) {
        m_error = QStringLiteral("Your purchases cannot be read: %1.").arg(error);
        emit changed();
        return false;
    }
    emit changed();
    return true;
}

std::optional<QString> PurchaseCheck::onReply(const QJsonObject &reply)
{
    const QString status = reply.value(QStringLiteral("status")).toString();
    if (m_checkingItem) {
        // The item page (found() stays set: the purchase was found either way).
        m_checkingItem = false;
        if (status != QLatin1String("ok")) {
            m_found->itemNote = status == QLatin1String("limit_reached")
                                    ? QStringLiteral("today's eBay page limit is reached; the item was not checked")
                                    : QStringLiteral("eBay did not show the item's page; the item was not checked");
            return std::nullopt;
        }
        const lookup::ItemRecord item = lookup::ItemRecord::fromJson(reply.value(QStringLiteral("result")).toObject());
        if (item.itemId != m_found->itemId) {
            m_found->itemNote = QStringLiteral("the page shown is not this item's; the item was not checked");
            return std::nullopt;
        }
        m_found->itemState = item.sold() ? QStringLiteral("sold")
                             : item.availability == QLatin1String("InStock") ? QStringLiteral("for sale")
                                                                              : QStringLiteral("unknown");
        if (item.sellerId.compare(m_sellerId, Qt::CaseInsensitive) != 0) {
            m_found->itemNote = item.sellerId.isEmpty() ? QStringLiteral("the item's page names no seller; the item was not checked")
                                                        : QStringLiteral("the item's page names another seller");
            return std::nullopt;
        }
        m_found->itemChecked = true;
        m_found->itemNote = QStringLiteral("item checked: this seller's item, %1 on eBay").arg(m_found->itemState);
        return std::nullopt;
    }
    if (status != QLatin1String("ok")) {
        m_error = status == QLatin1String("limit_reached")
                      ? QStringLiteral("Today's eBay page limit is reached; your purchases were not read.")
                      : QStringLiteral("eBay did not show your purchases (%1).")
                            .arg(reply.value(QStringLiteral("reason")).toString().isEmpty() ? reply.value(QStringLiteral("error")).toString()
                                                                                            : reply.value(QStringLiteral("reason")).toString());
        return std::nullopt;
    }
    const QJsonObject data = reply.value(QStringLiteral("result")).toObject();
    static const QRegularExpression itemPattern(QStringLiteral("^\\d{9,15}$"));
    static const QRegularExpression monthPattern(QStringLiteral("^\\d{4}-(0[1-9]|1[0-2])$"));
    for (const QJsonValue &value : data.value(QStringLiteral("orders")).toArray()) {
        const QJsonObject order = value.toObject();
        ++m_orders;
        if (order.value(QStringLiteral("sellerId")).toString().compare(m_sellerId, Qt::CaseInsensitive) != 0)
            continue;
        const QString itemId = order.value(QStringLiteral("itemId")).toString();
        const QString month = order.value(QStringLiteral("month")).toString();
        // An order without a readable item number or month proves nothing.
        if (!itemPattern.match(itemId).hasMatch() || !monthPattern.match(month).hasMatch())
            continue;
        m_found = Found{itemId, month, false, QStringLiteral("unknown"), QStringLiteral("the item was not checked")};
        // The item check: its page, one more page in this session.
        m_checkingItem = true;
        m_task.fetchNext(ebay::itemUrl(itemId), ebay::PageKind::Item);
        return std::nullopt;
    }
    m_page = data.value(QStringLiteral("page")).toInt(m_page + 1);
    if (data.value(QStringLiteral("hasNext")).toBool() && m_page < kMaxPages)
        return QStringLiteral("@%1").arg(m_page + 1);
    m_error = QStringLiteral("No purchase from this seller in your last %1 orders on eBay's Purchases page.").arg(m_orders);
    return std::nullopt;
}

} // namespace app::ui
