#pragma once

#include "model/ModelCatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace app::model {

// Downloads one model file into the work folder's models/ (DESIGN 3.6,
// item 3): from the catalog's pinned URL only, into "<file>.part" (continued
// with an HTTP Range request when a part is there), its SHA-256 checked at
// the end - a mismatch deletes the part - and only then renamed to the file.
class ModelDownload : public QObject {
    Q_OBJECT
public:
    ModelDownload(QString modelsDir, QObject *parent = nullptr);
    ~ModelDownload() override;

    // Tests only: where the file is fetched from instead of the catalog URL.
    void setSourceForTests(const QUrl &url) { m_testSource = url; }

    static QString filePath(const QString &modelsDir, const ModelInfo &info);
    // True when the finished file is there with the catalog's size (its
    // SHA-256 is checked by the runner at every load).
    static bool isDownloaded(const QString &modelsDir, const ModelInfo &info);
    // Deletes the file and any part of it; false and *error when it cannot.
    static bool remove(const QString &modelsDir, const ModelInfo &info, QString *error);

    void start(const ModelInfo &info);
    void cancel();
    bool busy() const { return m_reply != nullptr; }
    qint64 received() const { return m_received; }
    qint64 total() const { return m_total; }
    QString modelId() const { return m_info.id; }

signals:
    void progress();
    // ok, or why not ("stopped" when cancelled).
    void finished(bool ok, const QString &error);

private:
    void onReadyRead();
    void onFinished();
    void fail(const QString &error, bool keepPart);
    bool hashExistingPart(QString *error);

    QString m_dir;
    QUrl m_testSource;
    ModelInfo m_info;
    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_part;
    std::unique_ptr<QCryptographicHash> m_hash;
    qint64 m_received = 0;
    qint64 m_total = 0;
    bool m_cancelled = false;
    bool m_statusChecked = false; // the reply's status was looked at once
};

} // namespace app::model
