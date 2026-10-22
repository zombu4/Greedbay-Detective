#include "model/Prompts.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace app::model {

namespace {

// Measured 2026-10-20 (identity run, 19 titles); the model line changed
// 2026-10-21 (case 11: the title's model number before the seller's MPN;
// the 6 saved owner listings with their specifics: 6 of 6 brands and models
// right, the SG80 listing whose MPN says "PA-81" now "SG80").
const char kIdentitySystem[] =
    "You read one eBay listing title, and its item specifics if given, and name the item for sale.\n"
    "Answer with JSON only.\n"
    "- brand: the maker as the title or the specifics name it; \"\" if none is named.\n"
    "- model: the maker's model number exactly as the maker writes it (for example IM-13, 8904A, 2022, DSO2C10). When "
    "the title names a model number, take it from the title: item specifics such as MPN are typed by the seller and "
    "can be wrong. \"\" if the listing names none.\n"
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

// The grammars fix the answer's keys and their order, and allow white space
// between the JSON tokens (ws, as llama.cpp's grammars/json.gbnf does).
// Measured 2026-10-21 (Qwen3-4B Q4_K_M, the pinned engine): the model writes
// its JSON spaced over several lines; a grammar without white space forced
// it off its own tokens - after `":` only tokens starting with a quote were
// allowed, and the brand came out "People's Republic of China" for 5 of 6
// listings it names right without a grammar, and 14 of 456 sort answers
// changed (9 of them to "same item"). With ws: 6 of 6 brands as without a
// grammar, 312 of 312 sort answers the same.
// A string the grammar allows: no quote, no backslash, no control character
// (the character class of llama.cpp's grammars/json.gbnf without escapes),
// at most 80 characters. ws: at most 20 spaces, tabs or line breaks.
const char kIdentityGrammar[] =
    "root ::= \"{\" ws \"\\\"brand\\\"\" ws \":\" ws \"\\\"\" text \"\\\"\" ws \",\" ws \"\\\"model\\\"\" ws \":\" ws "
    "\"\\\"\" text \"\\\"\" ws \",\" ws \"\\\"product\\\"\" ws \":\" ws \"\\\"\" text \"\\\"\" ws \",\" ws "
    "\"\\\"confident\\\"\" ws \":\" ws (\"true\" | \"false\") ws \"}\"\n"
    "text ::= [^\"\\\\\\x7F\\x00-\\x1F]{0,80}\n"
    "ws ::= [ \\t\\n]{0,20}\n";

const char kSortGrammar[] =
    "root ::= \"{\" ws \"\\\"category\\\"\" ws \":\" ws \"\\\"\" (\"same item\" | \"documentation\" | \"part-fits\" | "
    "\"companion\" | \"accessory\" | \"lot\" | \"other model\" | \"unknown\") \"\\\"\" ws \"}\"\n"
    "ws ::= [ \\t\\n]{0,20}\n";

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
