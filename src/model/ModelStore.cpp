#include "model/ModelStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace app::model {

ModelStore::ModelStore(QString connectionName) : m_connectionName(std::move(connectionName)) {}

QString ModelStore::questionKey(const QString &modelId, const QString &promptVersion, const Prompt &prompt)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Length-prefixed parts: no two different questions give the same bytes.
    auto add = [&hash](const QString &text, bool special) {
        const QByteArray bytes = text.toUtf8();
        hash.addData(QByteArray::number(bytes.size()) + (special ? "s:" : "t:"));
        hash.addData(bytes);
    };
    add(modelId, false);
    add(promptVersion, false);
    for (const PromptPiece &piece : prompt)
        add(piece.text, piece.special);
    return QString::fromLatin1(hash.result().toHex());
}

std::optional<QString> ModelStore::answer(const QString &key) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("SELECT answer FROM model_answers WHERE question_key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toString();
}

bool ModelStore::save(const QString &key, const QString &modelId, const QString &promptVersion, const QString &answer,
                      QString *error) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral("INSERT INTO model_answers (question_key, model_id, prompt_version, answer, answered_at) "
                                 "VALUES (?, ?, ?, ?, ?) ON CONFLICT(question_key) DO UPDATE SET answer = excluded.answer, "
                                 "answered_at = excluded.answered_at"));
    query.addBindValue(key);
    query.addBindValue(modelId);
    query.addBindValue(promptVersion);
    query.addBindValue(answer);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (query.exec())
        return true;
    *error = query.lastError().text();
    return false;
}

} // namespace app::model
