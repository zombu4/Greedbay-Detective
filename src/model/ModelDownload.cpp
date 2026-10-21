#include "model/ModelDownload.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace app::model {

namespace {

constexpr int kStallTimeoutMs = 60 * 1000;
constexpr qint64 kHashChunk = 4 * 1024 * 1024;

QString partPath(const QString &file) { return file + QStringLiteral(".part"); }

} // namespace

ModelDownload::ModelDownload(QString modelsDir, QObject *parent)
    : QObject(parent), m_dir(std::move(modelsDir)), m_network(new QNetworkAccessManager(this))
{
}

ModelDownload::~ModelDownload()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

QString ModelDownload::filePath(const QString &modelsDir, const ModelInfo &info) { return QDir(modelsDir).filePath(info.fileName); }

bool ModelDownload::isDownloaded(const QString &modelsDir, const ModelInfo &info)
{
    const QFileInfo file(filePath(modelsDir, info));
    return file.isFile() && !file.isSymLink() && file.size() == info.sizeBytes;
}

bool ModelDownload::remove(const QString &modelsDir, const ModelInfo &info, QString *error)
{
    for (const QString &path : {filePath(modelsDir, info), partPath(filePath(modelsDir, info))}) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            *error = QStringLiteral("%1 cannot be deleted").arg(QDir::toNativeSeparators(path));
            return false;
        }
    }
    return true;
}

bool ModelDownload::hashExistingPart(QString *error)
{
    // A part from an earlier start: its bytes go into the hash first.
    m_part->seek(0);
    while (!m_part->atEnd()) {
        const QByteArray chunk = m_part->read(kHashChunk);
        if (chunk.isEmpty()) {
            *error = QStringLiteral("the partly downloaded file cannot be read");
            return false;
        }
        m_hash->addData(chunk);
    }
    return true;
}

void ModelDownload::start(const ModelInfo &info)
{
    if (busy())
        return;
    m_info = info;
    m_cancelled = false;
    m_total = info.sizeBytes;
    if (!QDir().mkpath(m_dir)) {
        fail(QStringLiteral("the models folder cannot be created"), false);
        return;
    }
    const QString path = partPath(filePath(m_dir, info));
    if (QFileInfo(path).isSymLink()) {
        fail(QStringLiteral("the partly downloaded file is a link; it is not used"), false);
        return;
    }
    m_part = std::make_unique<QFile>(path);
    if (!m_part->open(QIODevice::ReadWrite)) {
        fail(QStringLiteral("%1 cannot be written").arg(QDir::toNativeSeparators(path)), false);
        return;
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received = m_part->size();
    if (m_received > info.sizeBytes) {
        m_part->resize(0);
        m_received = 0;
    }
    QString error;
    if (m_received > 0 && !hashExistingPart(&error)) {
        fail(error, false);
        return;
    }
    m_part->seek(m_received);
    if (m_received == info.sizeBytes) {
        onFinished();
        return;
    }

    QNetworkRequest request(m_testSource.isEmpty() ? info.url : m_testSource);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kStallTimeoutMs);
    m_statusChecked = false;
    if (m_received > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_received) + '-');
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownload::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownload::onFinished);
    emit progress();
}

void ModelDownload::onReadyRead()
{
    if (!m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206)
        return; // the error is handled when it finishes
    if (!m_statusChecked) {
        m_statusChecked = true;
        if (status == 200 && m_received > 0) {
            // The whole file instead of the rest: the part starts again.
            m_part->resize(0);
            m_part->seek(0);
            m_hash->reset();
            m_received = 0;
        } else if (status == 206
                   && !m_reply->rawHeader("Content-Range").startsWith("bytes " + QByteArray::number(m_received) + '-')) {
            fail(QStringLiteral("the server sent another part of the file than asked; start the download again"), false);
            return;
        }
    }
    const QByteArray data = m_reply->readAll();
    if (m_received + data.size() > m_info.sizeBytes) {
        fail(QStringLiteral("the server sent more than the file's size"), false);
        return;
    }
    if (m_part->write(data) != data.size()) {
        fail(QStringLiteral("the disk is full or the file cannot be written"), true);
        return;
    }
    m_hash->addData(data);
    m_received += data.size();
    emit progress();
}

void ModelDownload::onFinished()
{
    if (m_reply) {
        onReadyRead();
        if (!m_reply)
            return; // failed while reading
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_cancelled) {
            fail(QStringLiteral("stopped"), true);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || (status != 200 && status != 206)) {
            fail(QStringLiteral("the download failed: %1").arg(reply->errorString()), true);
            return;
        }
    }
    if (m_received != m_info.sizeBytes) {
        fail(QStringLiteral("the download ended early (%1 of %2 bytes); start it again to continue")
                 .arg(m_received)
                 .arg(m_info.sizeBytes),
             true);
        return;
    }
    if (QString::fromLatin1(m_hash->result().toHex()) != m_info.sha256) {
        fail(QStringLiteral("the downloaded file is not the published one (SHA-256 differs); it was deleted"), false);
        return;
    }
    m_part->close();
    const QString finalPath = filePath(m_dir, m_info);
    QFile::remove(finalPath);
    if (!m_part->rename(finalPath)) {
        fail(QStringLiteral("the finished file cannot be renamed"), false);
        return;
    }
    m_part.reset();
    m_hash.reset();
    emit progress();
    emit finished(true, QString());
}

void ModelDownload::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void ModelDownload::fail(const QString &error, bool keepPart)
{
    if (m_reply) {
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_part) {
        m_part->close();
        if (!keepPart)
            m_part->remove();
    }
    m_part.reset();
    m_hash.reset();
    if (!keepPart)
        m_received = 0;
    emit progress();
    emit finished(false, error);
}

} // namespace app::model
