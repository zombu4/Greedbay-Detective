#pragma once

#include <QString>

namespace app::ui {

// Registers the bundled IBM Plex fonts (ui-spec "Look": IBM Plex Sans 400,
// 500, 600 for text; IBM Plex Mono 400, 500 for every price, number, date
// and item id), from :/fonts/. Families "IBM Plex Sans" and "IBM Plex Mono".
// Then makes Plex Sans the application's default font. False (and *error)
// when one cannot be registered (the others still are; the default font is
// then left as it is). Call after the application object exists.
bool loadFonts(QString *error);

} // namespace app::ui
