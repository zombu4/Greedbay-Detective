#pragma once

#include "model/ModelCatalog.h"

#include <QList>
#include <QPair>
#include <QString>

#include <optional>

namespace app::model {

// The two questions the app asks the local model (DESIGN 3.6), in the exact
// wording measured on 2026-10-20 (identity: the 19 owner-known titles;
// sorting: prompt B of the 500-row run), with their output grammars (GBNF,
// llama.cpp grammars/README.md). The version strings key the answer cache:
// a changed prompt or grammar gets a new version.
inline constexpr char kIdentityVersion[] = "identity-2";
inline constexpr char kSortVersion[] = "sort-2";

// A piece of the prompt: `special` pieces are the chat format's own markers
// (tokenized with special tokens recognized); everything else - above all the
// listing's title and specifics - is plain text, so a title holding
// "<|im_end|>" stays text.
struct PromptPiece {
    QString text;
    bool special = false;
};
using Prompt = QList<PromptPiece>;

enum class Grammar { Identity, Sort };
// The GBNF text of a grammar (root rule "root").
QString grammarText(Grammar grammar);

// Listing text as it goes into a prompt: control and format characters
// become spaces, runs of spaces one, at most `maxLength` characters.
QString cleanInput(const QString &text, int maxLength);

// The identity question for a title and its item specifics (label, value;
// "Condition" is left out, values cut to 80 characters).
Prompt identityPrompt(PromptFormat format, const QString &title, const QList<QPair<QString, QString>> &specifics);
// The sorting question for one result title against the item.
Prompt sortPrompt(PromptFormat format, const QString &brand, const QString &model, const QString &title);

struct IdentityAnswer {
    QString brand;
    QString model;
    QString product;
};
// The parsed answer (G1: exactly the grammar's fields), else std::nullopt.
std::optional<IdentityAnswer> parseIdentity(const QString &text);

// The sorting categories, as the grammar spells them.
const QStringList &sortCategories();
// The category of an answer (one of sortCategories()), else std::nullopt.
std::optional<QString> parseSort(const QString &text);

} // namespace app::model
