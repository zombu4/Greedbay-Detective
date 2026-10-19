#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace app::ui {

// Listing photos for QML as image://listing/<item number>[?<anything>]
// (the part after '?' only makes QML ask again after a photo changed).
// The photos come from memory only: the Backend puts each one in with
// setPhoto() (read from the database on the GUI thread), so this provider
// never touches the database or the network, from whatever thread Qt asks.
class PhotoProvider : public QQuickImageProvider {
public:
    PhotoProvider();

    void setPhoto(const QString &itemId, const QImage &image);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QHash<QString, QImage> m_photos;
};

} // namespace app::ui
