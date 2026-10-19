#include "lookup/LookupRunner.h"

#include "ebay/SearchUrls.h"

#include <QJsonArray>

#include <utility>

namespace app::lookup {

namespace {

QString pageText(const QJsonObject &reply)
{
    return QStringLiteral("%1 (%2)").arg(reply.value(QStringLiteral("page")).toString(),
                                         reply.value(QStringLiteral("reason")).toString());
}

} // namespace

LookupRunner::LookupRunner(Fetcher fetcher, QObject *parent)
    : QObject(parent), m_fetcher(std::move(fetcher))
{
}

bool LookupRunner::startItem(const QString &input, QString *error)
{
    if (m_state != State::Idle) {
        *error = QStringLiteral("a lookup is already running");
        return false;
    }
    m_itemId = ebay::itemIdFromInput(input);
    if (m_itemId.isEmpty()) {
        *error = QStringLiteral("This is not an eBay item address (https://www.ebay.com/itm/<number>) or a 12-digit "
                                "item number.");
        return false;
    }
    m_clock.start();
    m_state = State::Item;
    emit progress(QStringLiteral("reading the listing %1").arg(m_itemId));
    fetch(ebay::itemUrl(m_itemId), ebay::PageKind::Item);
    return true;
}

bool LookupRunner::startComps(const QStringList &queries, const Depths &depths, int dayRange, QString *error)
{
    if (m_state != State::WaitingForQueries) {
        *error = QStringLiteral("the listing has not been read yet");
        return false;
    }
    QStringList kept;
    for (const QString &query : queries) {
        if (!query.simplified().isEmpty())
            kept.append(query.simplified());
    }
    if (kept.isEmpty()) {
        *error = QStringLiteral("enter at least one search query");
        return false;
    }
    if (depths.sold < 0 || depths.active < 0 || depths.research < 0 || dayRange < 1) {
        *error = QStringLiteral("the depths cannot be negative and the period must be at least one day");
        return false;
    }
    m_result.queries = kept;
    m_depths = depths;
    m_dayRange = dayRange;
    m_state = State::Comps;
    m_source = Source::Sold;
    m_queryIndex = 0;
    m_page = 1;
    m_offset = 0;
    m_seenBefore.clear();
    m_seenNow.clear();
    nextFetch();
    return true;
}

void LookupRunner::fetch(const QUrl &url, ebay::PageKind kind)
{
    m_fetching = true;
    m_fetcher(url, kind);
}

void LookupRunner::onReply(const QJsonObject &reply)
{
    if (!m_fetching)
        return;
    m_fetching = false;
    const QString status = reply.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("ok") || status == QLatin1String("unexpected_page"))
        ++m_result.pagesLoaded;
    if (m_state == State::Item)
        onItemReply(reply);
    else if (m_state == State::Comps)
        onCompsReply(reply);
}

void LookupRunner::cancel()
{
    if (m_state == State::Idle || m_state == State::Done)
        return;
    if (m_fetching) {
        m_cancelRequested = true;
        emit progress(QStringLiteral("cancel: stopping after the page being loaded"));
        return;
    }
    finish(QStringLiteral("cancelled"), QStringLiteral("cancelled by the user"));
}

void LookupRunner::onItemReply(const QJsonObject &reply)
{
    const QString status = reply.value(QStringLiteral("status")).toString();
    if (status != QLatin1String("ok")) {
        if (status == QLatin1String("failed") && reply.value(QStringLiteral("error")).toString() == QLatin1String("stopped"))
            finish(QStringLiteral("cancelled"), QStringLiteral("cancelled by the user"));
        else if (status == QLatin1String("unexpected_page"))
            finish(QStringLiteral("stopped"), QStringLiteral("the listing page was not read: eBay showed %1").arg(pageText(reply)));
        else
            finish(QStringLiteral("stopped"), QStringLiteral("the listing page was not read: %1")
                                                  .arg(reply.value(QStringLiteral("error")).toString()));
        return;
    }
    m_result.item = ItemRecord::fromJson(reply.value(QStringLiteral("result")).toObject());
    if (m_result.item.itemId.isEmpty())
        m_result.notes.append(QStringLiteral("the item page that was read has no item number in its address (asked for %1)")
                                  .arg(m_itemId));
    m_result.identity = identify(m_result.item);
    m_result.queries = buildQueries(m_result.identity, m_result.item.title);
    if (m_cancelRequested) {
        finish(QStringLiteral("cancelled"), QStringLiteral("cancelled by the user"));
        return;
    }
    m_state = State::WaitingForQueries;
    emit progress(QStringLiteral("listing read: %1").arg(m_result.item.title));
    emit itemReady();
}

void LookupRunner::onCompsReply(const QJsonObject &reply)
{
    const QString status = reply.value(QStringLiteral("status")).toString();
    bool morePages = false;
    if (status == QLatin1String("ok")) {
        morePages = takePage(reply.value(QStringLiteral("result")).toObject());
    } else if (status == QLatin1String("unexpected_page") && m_source == Source::Research
               && reply.value(QStringLiteral("page")).toString() == QLatin1String("no_access")) {
        m_result.researchUnavailable = true;
        // Owner decision Q22: without Product Research access every price is
        // unconfirmed.
        for (SearchRecord &record : m_result.sold)
            record.confirmed = false;
        m_result.notes.append(QStringLiteral("Product Research: eBay sent the Seller Hub start page (no access on this "
                                             "account); research rows skipped and every sold price marked unconfirmed"));
        emit progress(m_result.notes.last());
        nextSource();
    } else if (status == QLatin1String("failed") && reply.value(QStringLiteral("error")).toString() == QLatin1String("stopped")) {
        finish(QStringLiteral("cancelled"), QStringLiteral("cancelled by the user"));
        return;
    } else if (status == QLatin1String("unexpected_page")) {
        QString message = QStringLiteral("%1 page not read: eBay showed %2").arg(sourceName(m_source), pageText(reply));
        if (m_source == Source::Research && reply.value(QStringLiteral("page")).toString() == QLatin1String("unexpected"))
            message += QStringLiteral("; for Product Research this can also be a query without sales in the period "
                                      "(eBay's page for that case has not been recorded yet)");
        finish(QStringLiteral("stopped"), message);
        return;
    } else {
        finish(QStringLiteral("stopped"), QStringLiteral("%1 page not read: %2")
                                              .arg(sourceName(m_source), reply.value(QStringLiteral("error")).toString()));
        return;
    }
    if (m_cancelRequested) {
        finish(QStringLiteral("cancelled"), QStringLiteral("cancelled by the user"));
        return;
    }
    if (status == QLatin1String("ok")) {
        if (morePages && collectedOf(m_source) < depthOf(m_source)) {
            if (m_source == Source::Research)
                m_offset += ebay::kResearchPageSize;
            else
                ++m_page;
        } else {
            nextQuery();
        }
    }
    nextFetch();
}

bool LookupRunner::takePage(const QJsonObject &data)
{
    const int room = depthOf(m_source) - collectedOf(m_source);
    int read = 0;
    int added = 0;
    int repeated = 0;
    QSet<QString> pageIds;
    // A listing an earlier page already gave is skipped.
    auto take = [&](const QString &itemId) {
        ++read;
        if (!itemId.isEmpty() && (m_seenBefore.contains(itemId) || m_seenNow.contains(itemId))) {
            ++repeated;
            return false;
        }
        if (added >= room)
            return false;
        if (!itemId.isEmpty())
            pageIds.insert(itemId);
        ++added;
        return true;
    };
    QJsonValue more;
    if (m_source == Source::Research) {
        if (m_result.researchSummary.isEmpty())
            m_result.researchSummary = data.value(QStringLiteral("summary")).toObject();
        for (const QJsonValue &value : data.value(QStringLiteral("rows")).toArray()) {
            const ResearchRecord record = ResearchRecord::fromJson(value.toObject());
            if (take(record.itemId))
                m_result.research.append(record);
        }
        more = data.value(QStringLiteral("morePossible"));
    } else {
        const bool sold = m_source == Source::Sold;
        QList<SearchRecord> &list = sold ? m_result.sold : m_result.active;
        for (const QJsonValue &value : data.value(QStringLiteral("items")).toArray()) {
            const SearchRecord record = SearchRecord::fromJson(value.toObject(), sold);
            if (take(record.itemId))
                list.append(record);
        }
        more = data.value(QStringLiteral("hasMorePages"));
    }
    m_seenNow.unite(pageIds);
    const QString where = m_source == Source::Research ? QStringLiteral("rows from %1").arg(m_offset)
                                                       : QStringLiteral("page %1").arg(m_page);
    emit progress(QStringLiteral("%1, query %2 \"%3\", %4: %5 read, %6 added, %7 already seen (%8 of up to %9)")
                      .arg(sourceName(m_source), QString::number(m_queryIndex + 1), m_result.queries.at(m_queryIndex),
                           where, QString::number(read), QString::number(added), QString::number(repeated),
                           QString::number(collectedOf(m_source)), QString::number(depthOf(m_source))));
    if (read > 0 && !more.isBool() && collectedOf(m_source) < depthOf(m_source)) {
        m_result.notes.append(QStringLiteral("%1, query %2, %3: the page does not tell whether more results exist; "
                                             "paging stopped there")
                                  .arg(sourceName(m_source), QString::number(m_queryIndex + 1), where));
        emit progress(m_result.notes.last());
    }
    return read > 0 && more.toBool(false);
}

void LookupRunner::nextQuery()
{
    m_seenBefore.unite(m_seenNow);
    m_seenNow.clear();
    ++m_queryIndex;
    m_page = 1;
    m_offset = 0;
}

void LookupRunner::nextSource()
{
    m_source = static_cast<Source>(static_cast<int>(m_source) + 1);
    m_queryIndex = 0;
    m_page = 1;
    m_offset = 0;
    m_seenBefore.clear();
    m_seenNow.clear();
}

void LookupRunner::nextFetch()
{
    while (m_source != Source::End) {
        const bool skip = depthOf(m_source) == 0 || collectedOf(m_source) >= depthOf(m_source)
                          || m_queryIndex >= m_result.queries.size()
                          || (m_source == Source::Research && m_result.researchUnavailable);
        if (skip) {
            nextSource();
            continue;
        }
        const QString &query = m_result.queries.at(m_queryIndex);
        switch (m_source) {
        case Source::Sold:
            fetch(ebay::soldSearchUrl(query, m_page), ebay::PageKind::SoldSearch);
            return;
        case Source::Active:
            fetch(ebay::activeSearchUrl(query, m_page), ebay::PageKind::ActiveSearch);
            return;
        case Source::Research:
            fetch(ebay::researchUrl(query, m_dayRange, m_offset), ebay::PageKind::Research);
            return;
        case Source::End:
            break;
        }
    }
    finish(QStringLiteral("complete"), QString());
}

int LookupRunner::depthOf(Source source) const
{
    switch (source) {
    case Source::Sold:
        return m_depths.sold;
    case Source::Active:
        return m_depths.active;
    case Source::Research:
        return m_depths.research;
    case Source::End:
        break;
    }
    return 0;
}

int LookupRunner::collectedOf(Source source) const
{
    switch (source) {
    case Source::Sold:
        return static_cast<int>(m_result.sold.size());
    case Source::Active:
        return static_cast<int>(m_result.active.size());
    case Source::Research:
        return static_cast<int>(m_result.research.size());
    case Source::End:
        break;
    }
    return 0;
}

QString LookupRunner::sourceName(Source source) const
{
    switch (source) {
    case Source::Sold:
        return QStringLiteral("sold search");
    case Source::Active:
        return QStringLiteral("for sale now");
    case Source::Research:
        return QStringLiteral("Product Research");
    case Source::End:
        break;
    }
    return {};
}

void LookupRunner::finish(const QString &status, const QString &message)
{
    m_state = State::Done;
    m_fetching = false;
    m_result.status = status;
    m_result.message = message;
    m_result.elapsedMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    emit progress(message.isEmpty() ? QStringLiteral("lookup %1").arg(status)
                                    : QStringLiteral("lookup %1: %2").arg(status, message));
    emit finished();
}

} // namespace app::lookup
