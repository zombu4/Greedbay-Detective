#include "ItemCheck.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

namespace sellerserver {

namespace {

bool hasItemPageJsonLd(const QByteArray &html)
{
    static const QRegularExpression block(QStringLiteral("<script[^>]*type=\"application/ld\\+json\"[^>]*>(.*?)</script>"),
                                          QRegularExpression::DotMatchesEverythingOption
                                              | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression itemPage(QStringLiteral("\"@type\"\\s*:\\s*\"ItemPage\""));
    const QString text = QString::fromUtf8(html);
    QRegularExpressionMatchIterator it = block.globalMatch(text);
    while (it.hasNext()) {
        if (itemPage.match(it.next().captured(1)).hasMatch())
            return true;
    }
    return false;
}

// The username= of every seller feedback link of this item.
QSet<QString> sellerLinks(const QByteArray &html, const QString &itemId)
{
    static const QRegularExpression link(QStringLiteral("href=\"([^\"]*/fdbk/mweb_profile\\?[^\"]*)\""));
    QSet<QString> sellers;
    QRegularExpressionMatchIterator it = link.globalMatch(QString::fromUtf8(html));
    while (it.hasNext()) {
        QString href = it.next().captured(1);
        href.replace(QLatin1String("&amp;"), QLatin1String("&"));
        const QUrlQuery query(href.mid(href.indexOf(QLatin1Char('?')) + 1));
        if (query.queryItemValue(QStringLiteral("fdbkType")) != QLatin1String("FeedbackReceivedAsSeller")
            || query.queryItemValue(QStringLiteral("item_id")) != itemId)
            continue;
        const QString seller = query.queryItemValue(QStringLiteral("username"), QUrl::FullyDecoded);
        if (!seller.isEmpty())
            sellers.insert(seller.toLower());
    }
    return sellers;
}

} // namespace

ItemReading readItemPage(const QByteArray &html, const QString &itemId, const QString &sellerId)
{
    if (!hasItemPageJsonLd(html))
        return {ItemVerdict::NotReadable, QStringLiteral("not an item page (no ItemPage data)")};
    const QSet<QString> sellers = sellerLinks(html, itemId);
    if (sellers.isEmpty())
        return {ItemVerdict::NotReadable, QStringLiteral("no seller link for this item on the page")};
    if (sellers.size() > 1)
        return {ItemVerdict::NotReadable, QStringLiteral("the page names more than one seller for this item")};
    if (*sellers.cbegin() != sellerId.toLower())
        return {ItemVerdict::OtherSeller, QStringLiteral("the item page names seller %1").arg(*sellers.cbegin())};
    return {ItemVerdict::Confirmed, {}};
}

ItemPageFetcher::ItemPageFetcher(Settings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)), m_network(new QNetworkAccessManager(this)), m_gap(new QTimer(this))
{
    m_gap->setSingleShot(true);
    connect(m_gap, &QTimer::timeout, this, &ItemPageFetcher::startNext);
}

ItemPageFetcher::~ItemPageFetcher()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}

void ItemPageFetcher::check(const QString &reportId, const QString &itemId, const QString &sellerId)
{
    if (m_queue.size() >= m_settings.maxQueue) {
        emit finished(reportId, {ItemVerdict::NotReadable, QStringLiteral("item check queue full")});
        return;
    }
    m_queue.append({reportId, itemId, sellerId});
    if (!m_reply && !m_gap->isActive())
        startNext();
}

void ItemPageFetcher::startNext()
{
    if (m_reply || m_queue.isEmpty())
        return;
    if (m_pausedUntil.isValid() && QDateTime::currentDateTimeUtc() < m_pausedUntil) {
        const QList<Job> jobs = std::exchange(m_queue, {});
        for (const Job &job : jobs)
            emit finished(job.reportId, {ItemVerdict::NotReadable, QStringLiteral("eBay refused the server's page loads (paused)")});
        return;
    }
    QUrl url = m_settings.base;
    url.setPath(QStringLiteral("/itm/") + m_queue.first().itemId);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(m_settings.timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_settings.userAgent);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (m_reply && received > m_settings.maxBytes)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, &ItemPageFetcher::onReplyFinished);
}

void ItemPageFetcher::onReplyFinished()
{
    QNetworkReply *reply = std::exchange(m_reply, nullptr);
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403 || status == 429) {
        m_pausedUntil = QDateTime::currentDateTimeUtc().addMSecs(m_settings.pauseMs);
        finishJob({ItemVerdict::NotReadable, QStringLiteral("eBay answered HTTP %1 (paused)").arg(status)});
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        finishJob({ItemVerdict::NotReadable, QStringLiteral("page load failed: %1").arg(reply->errorString())});
        return;
    }
    if (status != 200) {
        finishJob({ItemVerdict::NotReadable, QStringLiteral("eBay answered HTTP %1").arg(status)});
        return;
    }
    const Job &job = m_queue.first();
    finishJob(readItemPage(reply->readAll(), job.itemId, job.sellerId));
}

void ItemPageFetcher::finishJob(const ItemReading &reading)
{
    const Job job = m_queue.takeFirst();
    m_gap->start(m_settings.gapMs);
    emit finished(job.reportId, reading);
}

} // namespace sellerserver
