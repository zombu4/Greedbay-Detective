#include "model/Prompts.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace app::model {

namespace {

// Measured 2026-10-20 (identity run, 19 titles), verbatim.
const char kIdentitySystem[] =
    "You read one eBay listing title, and its item specifics if given, and name the item for sale.\n"
    "Answer with JSON only.\n"
    "- brand: the maker as the title or the specifics name it; \"\" if none is named.\n"
    "- model: the maker's model number exactly as the maker writes it (for example IM-13, 8904A, 2022, DSO2C10); \"\" if "
    "the listing names none.\n"
    "- product: what the item is, in 1 to 4 words (for example \"signal generator\", \"tube amplifier\", \"radio\").\n"
    "- confident: true only if the model number appears in the title itself.\n"
    "Never use as brand or model: a specification (100MHz, 2CH, 10kHz-1000MHz, 7\", 500 V/div, 1500V), a seller's stock "
    "code, an ASIN, UPC, EAN or GTIN, or a value such as \"Unbranded\", \"Generic\", \"Does not apply\".";

// Measured 2026-10-20 (sorting run, prompt B, 500 rows), verbatim.
const char kSortSystem[] =
    "A buyer searched eBay for one exact product. Decide what one search result is, relative to that product.\n"
    "Categories (choose exactly one): \"other model\" = a different model or a different maker's product; \"lot\" = "
    "several units or a mixed bundle; \"accessory\" = a cable, probe, controller, power supply or case on its own; "
    "\"companion\" = a separate product made for the product, like a game, software, add-on or upgrade; \"part-fits\" "
    "= a spare part, or a part that fits several models; \"documentation\" = a manual, schematic, book, magazine, "
    "catalog or ad; \"same item\" = the product itself in any condition, possibly with its usual accessories; "
    "\"unknown\" = not enough information.\n"
    "Reply with JSON only.";

// A string the grammar allows: no quote, no backslash, no control character
// (the character class of llama.cpp's grammars/json.gbnf without escapes),
// at most 80 characters.
const char kIdentityGrammar[] =
    "root ::= \"{\\\"brand\\\":\\\"\" text \"\\\",\\\"model\\\":\\\"\" text \"\\\",\\\"product\\\":\\\"\" text "
    "\"\\\",\\\"confident\\\":\" (\"true\" | \"false\") \"}\"\n"
    "text ::= [^\"\\\\\\x7F\\x00-\\x1F]{0,80}\n";

const char kSortGrammar[] =
    "root ::= \"{\\\"category\\\":\\\"\" (\"same item\" | \"documentation\" | \"part-fits\" | \"companion\" | "
    "\"accessory\" | \"lot\" | \"other model\" | \"unknown\") \"\\\"}\"\n";

constexpr int kMaxTitle = 200;
constexpr int kMaxSpecificValue = 80;
constexpr int kMaxSpecifics = 30;

Prompt chat(PromptFormat format, const QString &system, const QString &user)
{
    Prompt prompt{{QStringLiteral("<|im_start|>system\n"), true},
                  {system, false},
                  {QStringLiteral("<|im_end|>\n<|im_start|>user\n"), true},
                  {user, false},
                  {QStringLiteral("<|im_end|>\n<|im_start|>assistant\n"), true}};
    if (format == PromptFormat::Qwen3NoThinking)
        prompt.append({QStringLiteral("<think>\n\n</think>\n\n"), true});
    return prompt;
}

} // namespace

QString grammarText(Grammar grammar)
{
    return QString::fromLatin1(grammar == Grammar::Identity ? kIdentityGrammar : kSortGrammar);
}

QString cleanInput(const QString &text, int maxLength)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const QChar::Category category = c.category();
        out.append(category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line
                           || category == QChar::Separator_Paragraph
                       ? QLatin1Char(' ')
                       : c);
    }
    return out.simplified().left(maxLength);
}

Prompt identityPrompt(PromptFormat format, const QString &title, const QList<QPair<QString, QString>> &specifics)
{
    QString user = QStringLiteral("Title: ") + cleanInput(title, kMaxTitle);
    QStringList parts;
    for (const auto &[label, value] : specifics) {
        if (label.compare(QLatin1String("Condition"), Qt::CaseInsensitive) == 0 || parts.size() >= kMaxSpecifics)
            continue;
        parts.append(cleanInput(label, 40) + QStringLiteral(": ") + cleanInput(value, kMaxSpecificValue));
    }
    if (!parts.isEmpty())
        user += QStringLiteral("\nItem specifics: ") + parts.join(QStringLiteral("; "));
    return chat(format, QString::fromUtf8(kIdentitySystem), user);
}

Prompt sortPrompt(PromptFormat format, const QString &brand, const QString &model, const QString &title)
{
    const QString user = QStringLiteral("Product: %1 %2\nSearch result: %3")
                             .arg(cleanInput(brand, 60), cleanInput(model, 60), cleanInput(title, kMaxTitle));
    return chat(format, QString::fromUtf8(kSortSystem), user);
}

std::optional<IdentityAnswer> parseIdentity(const QString &text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.size() != 4 || !object.value(QStringLiteral("brand")).isString() || !object.value(QStringLiteral("model")).isString()
        || !object.value(QStringLiteral("product")).isString() || !object.value(QStringLiteral("confident")).isBool())
        return std::nullopt;
    return IdentityAnswer{object.value(QStringLiteral("brand")).toString().simplified(),
                          object.value(QStringLiteral("model")).toString().simplified(),
                          object.value(QStringLiteral("product")).toString().simplified()};
}

const QStringList &sortCategories()
{
    static const QStringList categories{QStringLiteral("same item"), QStringLiteral("documentation"), QStringLiteral("part-fits"),
                                        QStringLiteral("companion"), QStringLiteral("accessory"),     QStringLiteral("lot"),
                                        QStringLiteral("other model"), QStringLiteral("unknown")};
    return categories;
}

std::optional<QString> parseSort(const QString &text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject() || document.object().size() != 1)
        return std::nullopt;
    const QString category = document.object().value(QStringLiteral("category")).toString();
    if (!sortCategories().contains(category))
        return std::nullopt;
    return category;
}

} // namespace app::model
