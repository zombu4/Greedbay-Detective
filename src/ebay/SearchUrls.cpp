#include "ebay/SearchUrls.h"

#include "ebay/PageCheck.h"

#include <QRegularExpression>

namespace app::ebay {

namespace {

// The query as eBay writes it in _nkw: percent-encoded, a space as "+".
QString searchTerms(const QString &query)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(query.simplified()).replace("%20", "+"));
}

QUrl searchUrl(const QString &query, int page, bool sold)
{
    QString text = QStringLiteral("https://www.ebay.com/sch/i.html?_nkw=") + searchTerms(query);
    if (sold)
        text += QStringLiteral("&LH_Sold=1&LH_Complete=1");
    text += QStringLiteral("&_ipg=%1").arg(kSearchPageSize);
    if (page > 1)
        text += QStringLiteral("&_pgn=%1").arg(page);
    return QUrl(text, QUrl::StrictMode);
}

} // namespace

QUrl activeSearchUrl(const QString &query, int page)
{
    return searchUrl(query, page, false);
}

QUrl soldSearchUrl(const QString &query, int page)
{
    return searchUrl(query, page, true);
}

QUrl researchUrl(const QString &query, int dayRange, int offset)
{
    return QUrl(QStringLiteral("https://www.ebay.com/sh/research?marketplace=EBAY-US&keywords=")
                    + QString::fromLatin1(QUrl::toPercentEncoding(query.simplified()))
                    + QStringLiteral("&dayRange=%1&tabName=SOLD&limit=%2&offset=%3")
                          .arg(dayRange)
                          .arg(kResearchPageSize)
                          .arg(offset),
                QUrl::StrictMode);
}

QUrl itemUrl(const QString &itemId)
{
    return QUrl(QStringLiteral("https://www.ebay.com/itm/") + itemId, QUrl::StrictMode);
}

QString itemIdFromInput(const QString &input)
{
    const QString text = input.trimmed();
    static const QRegularExpression bareNumber(QStringLiteral("^\\d{12}$"));
    if (bareNumber.match(text).hasMatch())
        return text;
    return itemIdFromUrl(QUrl(text, QUrl::StrictMode));
}

} // namespace app::ebay
