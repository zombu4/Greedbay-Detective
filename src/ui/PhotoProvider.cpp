#include "ui/PhotoProvider.h"

#include <QMutexLocker>

namespace app::ui {

PhotoProvider::PhotoProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

void PhotoProvider::setPhoto(const QString &itemId, const QImage &image)
{
    const QMutexLocker lock(&m_mutex);
    m_photos.insert(itemId, image);
}

QImage PhotoProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image;
    {
        const QMutexLocker lock(&m_mutex);
        image = m_photos.value(id.section(QLatin1Char('?'), 0, 0));
    }
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}

} // namespace app::ui
