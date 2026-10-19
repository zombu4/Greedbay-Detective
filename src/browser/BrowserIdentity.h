#pragma once

#include <QString>

namespace app::browser {

// The user agent the app's browser announces (owner decision): a normal
// Chrome identity matching the real engine, without the QtWebEngine token.
// Takes the engine's own default user agent and removes only the
// "QtWebEngine/<version>" token; the Chrome version, operating system and CPU
// parts stay exactly as the engine reports them, so no version is ever
// written into the code. Nothing else is changed: no other header, no
// fingerprint changes.
//
// Client Hints need no change: the engine's default brand list (measured on
// Qt WebEngine 6.140.0: "Chromium" with the engine's version and the
// "Not=A?Brand" placeholder) carries no QtWebEngine brand.
QString browserUserAgent(const QString &engineUserAgent);

} // namespace app::browser
