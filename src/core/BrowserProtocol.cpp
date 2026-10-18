#include "core/BrowserProtocol.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace app::browserprotocol {

QByteArray encode(const QJsonObject &message)
{
    // Compact JSON never contains a raw newline: newlines inside strings are
    // written as \n escapes.
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

void LineReader::append(const QByteArray &bytes)
{
    if (!m_broken)
        m_buffer += bytes;
}

std::optional<QJsonObject> LineReader::next(QString *error)
{
    if (m_broken)
        return std::nullopt;
    const qsizetype end = m_buffer.indexOf('\n');
    if (end < 0) {
        if (m_buffer.size() > kMaxLineBytes) {
            m_broken = true;
            m_buffer.clear();
            *error = QStringLiteral("protocol line too long");
        }
        return std::nullopt;
    }
    if (end > kMaxLineBytes) {
        m_broken = true;
        m_buffer.clear();
        *error = QStringLiteral("protocol line too long");
        return std::nullopt;
    }
    const QByteArray line = m_buffer.left(end);
    m_buffer.remove(0, end + 1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_broken = true;
        m_buffer.clear();
        *error = QStringLiteral("protocol line is not a JSON object");
        return std::nullopt;
    }
    return document.object();
}

} // namespace app::browserprotocol
