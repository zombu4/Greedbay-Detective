#pragma once

#include "core/WorkFolder.h"
#include "model/ModelDownload.h"

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace app::ui {

// Settings' "Local model" section and the first-run offer on Home (DESIGN
// 3.6, items 3 and 6): on/off (default off), the model, the graphics chip,
// download (the license and the needs shown first) and delete.
class ModelController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the backend")
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    // [{id, name, sizeText, memoryText, license, recommended, downloaded}]
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
    // The chosen model is downloaded, so the switch can be turned on.
    Q_PROPERTY(bool canEnable READ canEnable NOTIFY changed)
    Q_PROPERTY(bool gpu READ gpu NOTIFY changed)
    // This build can use the graphics chip (Apple Metal).
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable CONSTANT)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(QString downloadingId READ downloadingId NOTIFY changed)
    Q_PROPERTY(QString progressText READ progressText NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool messageOk READ messageOk NOTIFY changed)
    // Home's one-time offer: not answered yet and the model is off.
    Q_PROPERTY(bool offerVisible READ offerVisible NOTIFY changed)
    Q_PROPERTY(QString offerText READ offerText CONSTANT)
    // Why this computer cannot run the engine (a processor without AVX2...),
    // else empty.
    Q_PROPERTY(QString unsupportedText READ unsupportedText CONSTANT)

public:
    ModelController(WorkFolder folder, std::function<bool()> lookupRunning, QObject *parent = nullptr);

    bool enabled() const;
    QString selectedId() const;
    QVariantList models() const;
    bool canEnable() const;
    bool gpu() const;
    bool gpuAvailable() const;
    bool downloading() const { return m_download.busy(); }
    QString downloadingId() const { return m_download.busy() ? m_download.modelId() : QString(); }
    QString progressText() const;
    double progress() const;
    QString message() const { return m_message; }
    bool messageOk() const { return m_messageOk; }
    bool offerVisible() const;
    QString offerText() const;
    QString unsupportedText() const;

    Q_INVOKABLE bool setEnabled(bool enabled);
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void setGpu(bool enabled);
    // The license text as the model's repository has it.
    Q_INVOKABLE QString licenseText(const QString &id) const;
    // The local model's lines of the work folder's log (one per question:
    // prompt version, item number, answer, guard, result, time), oldest
    // first, at most the last kMaxLogLines, from the current log file and the
    // one before it; a sentence when there are none.
    static constexpr int kMaxLogLines = 300;
    Q_INVOKABLE QString modelLog() const;
    Q_INVOKABLE void download(const QString &id);
    Q_INVOKABLE void cancelDownload() { m_download.cancel(); }
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE void answerOffer();

signals:
    void changed();

private:
    void setMessage(const QString &message, bool ok);

    WorkFolder m_folder;
    std::function<bool()> m_lookupRunning;
    model::ModelDownload m_download;
    QString m_message;
    bool m_messageOk = true;
};

} // namespace app::ui
