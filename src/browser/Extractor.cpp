#include "browser/Extractor.h"

#include "ebay/PageCheck.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QWebEnginePage>
#include <QWebEngineScript>

namespace app::browser {

namespace {

QString resourceText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString kindScriptPath(ebay::PageKind kind)
{
    switch (kind) {
    case ebay::PageKind::ActiveSearch:
    case ebay::PageKind::SoldSearch:
        return QStringLiteral(":/extractors/search.js");
    case ebay::PageKind::Item:
        return QStringLiteral(":/extractors/item.js");
    case ebay::PageKind::Research:
        return QStringLiteral(":/extractors/research.js");
    }
    return {};
}

QJsonObject outcome(const char *page, const QString &reason)
{
    return QJsonObject{{QStringLiteral("page"), QString::fromLatin1(page)}, {QStringLiteral("reason"), reason}};
}

} // namespace

QString Extractor::script(ebay::PageKind kind, const QUrl &url)
{
    const QJsonObject input{{QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                            {QStringLiteral("kind"), ebay::pageKindName(kind)},
                            {QStringLiteral("itemId"), ebay::itemIdFromUrl(url)}};
    // JSON is valid JavaScript, so the input is passed as a literal.
    const QString inputLiteral = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    return QStringLiteral("(function (input) {\n'use strict';\n") + resourceText(QStringLiteral(":/extractors/common.js"))
           + QLatin1Char('\n') + resourceText(kindScriptPath(kind))
           + QStringLiteral("\nreturn JSON.stringify(extract(input));\n})(") + inputLiteral + QStringLiteral(");");
}

QJsonObject Extractor::parseResult(const QVariant &value)
{
    if (value.typeId() != QMetaType::QString)
        return outcome("invalid", QStringLiteral("the extractor returned no text"));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return outcome("invalid", QStringLiteral("the extractor result is not a JSON object"));
    const QJsonObject result = document.object();
    const QString page = result.value(QStringLiteral("page")).toString();
    if (page == QLatin1String("expected")) {
        if (!result.value(QStringLiteral("data")).isObject())
            return outcome("invalid", QStringLiteral("the extractor result has no data"));
        return QJsonObject{{QStringLiteral("page"), page}, {QStringLiteral("data"), result.value(QStringLiteral("data"))}};
    }
    if (page == QLatin1String("error_page") || page == QLatin1String("unexpected"))
        return outcome(page == QLatin1String("error_page") ? "error_page" : "unexpected",
                       result.value(QStringLiteral("reason")).toString());
    return outcome("invalid", QStringLiteral("the extractor result names no known page type"));
}

void Extractor::run(QWebEnginePage *page, ebay::PageKind kind, std::function<void(const QJsonObject &)> done)
{
    const QUrl url = page->url();
    switch (ebay::classifyUrl(url)) {
    case ebay::UrlClass::SignIn:
        done(outcome("sign_in", QStringLiteral("eBay showed its sign-in page")));
        return;
    case ebay::UrlClass::ResearchLanding:
        if (kind == ebay::PageKind::Research) {
            done(outcome("no_access", QStringLiteral("eBay sent the Seller Hub start page instead of Product Research")));
            return;
        }
        break;
    case ebay::UrlClass::AccountPage:
    case ebay::UrlClass::Other:
        break;
    }
    // The isolated ApplicationWorld: page scripts cannot interfere with the
    // extractor's functions; the DOM is shared.
    page->runJavaScript(script(kind, url), QWebEngineScript::ApplicationWorld,
                        [done = std::move(done)](const QVariant &value) { done(parseResult(value)); });
}

} // namespace app::browser
