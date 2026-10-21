#pragma once

#include "model/Prompts.h"

#include <QString>

#include <optional>

namespace app::model {

// The local model's answers in the database (migration 0006, model_answers).
class ModelStore {
public:
    explicit ModelStore(QString connectionName);

    // The cache key of a question: SHA-256 hex of the model id, the prompt
    // version and every piece of the prompt.
    static QString questionKey(const QString &modelId, const QString &promptVersion, const Prompt &prompt);

    // The stored answer; std::nullopt when there is none (or on a database
    // error, which then only costs a new question).
    std::optional<QString> answer(const QString &key) const;
    bool save(const QString &key, const QString &modelId, const QString &promptVersion, const QString &answer,
              QString *error) const;

private:
    QString m_connectionName;
};

} // namespace app::model
