#include "ui/Fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

namespace app::ui {

bool loadFonts(QString *error)
{
    const char *files[] = {"IBMPlexSans-Regular.otf", "IBMPlexSans-Medium.otf", "IBMPlexSans-SemiBold.otf",
                           "IBMPlexMono-Regular.otf", "IBMPlexMono-Medium.otf"};
    QStringList failed;
    for (const char *file : files) {
        if (QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + QLatin1String(file)) < 0)
            failed.append(QLatin1String(file));
    }
    if (!failed.isEmpty()) {
        *error = QStringLiteral("fonts not registered: %1").arg(failed.join(QStringLiteral(", ")));
        return false;
    }
    // Every text without its own font uses Plex Sans too.
    QFont font = QGuiApplication::font();
    font.setFamilies({QStringLiteral("IBM Plex Sans")});
    QGuiApplication::setFont(font);
    return true;
}

} // namespace app::ui
