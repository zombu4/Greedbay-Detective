#include "ui/ModelController.h"

#include "core/Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStorageInfo>

namespace app::ui {

namespace {

QString gigabytes(qint64 bytes) { return QStringLiteral("%1 GB").arg(QLocale().toString(static_cast<double>(bytes) / 1e9, 'f', 1)); }

} // namespace

ModelController::ModelController(WorkFolder folder, std::function<bool()> lookupRunning, QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_lookupRunning(std::move(lookupRunning)), m_download(m_folder.modelsDir())
{
    connect(&m_download, &model::ModelDownload::progress, this, &ModelController::changed);
    connect(&m_download, &model::ModelDownload::finished, this, [this](bool ok, const QString &error) {
        if (ok) {
            setMessage(tr("Downloaded and checked (SHA-256). Turn the local model on above to use it."), true);
        } else {
            setMessage(error == QLatin1String("stopped") ? tr("Download stopped; Download continues it.")
                                                          : tr("Download failed: %1").arg(error),
                       false);
        }
    });
}

bool ModelController::enabled() const { return Settings(m_folder.settingsPath()).modelEnabled(); }

QString ModelController::selectedId() const
{
    const QString id = Settings(m_folder.settingsPath()).modelId();
    return model::modelById(id) ? id : model::defaultModelId();
}

QVariantList ModelController::models() const
{
    QVariantList list;
    for (const model::ModelInfo &info : model::catalog()) {
        list.append(QVariantMap{{QStringLiteral("id"), info.id},
                                {QStringLiteral("name"), info.name},
                                {QStringLiteral("sizeText"), gigabytes(info.sizeBytes)},
                                {QStringLiteral("memoryText"),
                                 info.memoryBytes > 0 ? tr("about %1 of memory while it works").arg(gigabytes(info.memoryBytes))
                                                      : tr("memory use not measured (at least the file size)")},
                                {QStringLiteral("license"), info.license},
                                {QStringLiteral("recommended"), info.recommended},
                                {QStringLiteral("downloaded"), model::ModelDownload::isDownloaded(m_folder.modelsDir(), info)}});
    }
    return list;
}

QString ModelController::unsupportedText() const
{
    QString missing;
    if (model::processorSupportsEngine(&missing))
        return {};
    return tr("This processor lacks %1, which the local model's engine needs; it cannot run on this computer.").arg(missing);
}

bool ModelController::canEnable() const
{
    if (!unsupportedText().isEmpty())
        return false;
    const std::optional<model::ModelInfo> info = model::modelById(selectedId());
    return info && model::ModelDownload::isDownloaded(m_folder.modelsDir(), *info);
}

bool ModelController::gpu() const { return Settings(m_folder.settingsPath()).modelGpu(); }

bool ModelController::gpuAvailable() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

QString ModelController::progressText() const
{
    if (!m_download.busy())
        return {};
    return tr("%1 of %2").arg(gigabytes(m_download.received()), gigabytes(m_download.total()));
}

double ModelController::progress() const
{
    return m_download.total() > 0 ? static_cast<double>(m_download.received()) / static_cast<double>(m_download.total()) : 0.0;
}

bool ModelController::offerVisible() const
{
    if (!unsupportedText().isEmpty())
        return false;
    const Settings settings(m_folder.settingsPath());
    return !settings.modelOfferAnswered() && !settings.modelEnabled();
}

QString ModelController::offerText() const
{
    const model::ModelInfo info = *model::modelById(model::defaultModelId());
    return tr("New: a local model can double-check the identity of a listing and the sorting of the results. It runs on "
              "this computer only and is off until you turn it on. It needs a one-time download of %1 (%2, %3 license) "
              "and %4.")
        .arg(gigabytes(info.sizeBytes), info.name, info.license,
             info.memoryBytes > 0 ? tr("about %1 of memory while it works").arg(gigabytes(info.memoryBytes)) : QString());
}

bool ModelController::setEnabled(bool on)
{
    if (on && !canEnable()) {
        setMessage(tr("Download the model first."), false);
        return false;
    }
    if (m_lookupRunning && m_lookupRunning()) {
        setMessage(tr("Wait until the lookup is finished."), false);
        return false;
    }
    Settings settings(m_folder.settingsPath());
    settings.setModelEnabled(on);
    settings.setModelOfferAnswered();
    settings.sync();
    setMessage(on ? tr("On: the next lookup uses the local model.") : tr("Off."), true);
    return true;
}

void ModelController::select(const QString &id)
{
    if (!model::modelById(id) || (m_lookupRunning && m_lookupRunning()))
        return;
    Settings settings(m_folder.settingsPath());
    settings.setModelId(id);
    // Another model may not be downloaded yet: off until it is.
    if (!model::ModelDownload::isDownloaded(m_folder.modelsDir(), *model::modelById(id)))
        settings.setModelEnabled(false);
    settings.sync();
    emit changed();
}

void ModelController::setGpu(bool on)
{
    Settings settings(m_folder.settingsPath());
    settings.setModelGpu(on);
    settings.sync();
    emit changed();
}

QString ModelController::licenseText(const QString &id) const
{
    const std::optional<model::ModelInfo> info = model::modelById(id);
    if (!info)
        return {};
    QFile file(info->licenseResource);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void ModelController::download(const QString &id)
{
    const std::optional<model::ModelInfo> info = model::modelById(id);
    if (!info || m_download.busy())
        return;
    const QStorageInfo storage(m_folder.path());
    const qint64 part = QFileInfo(model::ModelDownload::filePath(m_folder.modelsDir(), *info) + QStringLiteral(".part")).size();
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < info->sizeBytes - part) {
        setMessage(tr("Not enough free disk space: %1 needed.").arg(gigabytes(info->sizeBytes - part)), false);
        return;
    }
    setMessage(QString(), true);
    m_download.start(*info);
}

bool ModelController::remove(const QString &id)
{
    const std::optional<model::ModelInfo> info = model::modelById(id);
    if (!info)
        return false;
    if (m_lookupRunning && m_lookupRunning()) {
        setMessage(tr("Wait until the lookup is finished."), false);
        return false;
    }
    if (m_download.busy() && m_download.modelId() == id)
        m_download.cancel();
    QString error;
    if (!model::ModelDownload::remove(m_folder.modelsDir(), *info, &error)) {
        setMessage(error, false);
        return false;
    }
    if (id == selectedId()) {
        Settings settings(m_folder.settingsPath());
        settings.setModelEnabled(false);
        settings.sync();
    }
    setMessage(tr("Deleted."), true);
    return true;
}

void ModelController::answerOffer()
{
    Settings settings(m_folder.settingsPath());
    settings.setModelOfferAnswered();
    settings.sync();
    emit changed();
}

void ModelController::setMessage(const QString &message, bool ok)
{
    m_message = message;
    m_messageOk = ok;
    emit changed();
}

} // namespace app::ui
