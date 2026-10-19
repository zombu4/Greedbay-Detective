#include "browser/BrowserCatalog.h"

#include <QFileInfo>
#include <QStringList>

#if defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <CoreServices/CoreServices.h>
#elif defined(Q_OS_WIN)
#  include <QSettings>
#else
#  include <QStandardPaths>
#endif

namespace app::browser {

namespace {

// One supported browser and the fixed names it is looked up by.
struct Candidate {
    const char *id;
    const char *name;
    // macOS bundle identifier. com.brave.Browser is read from Brave's own
    // Info.plist; the others are checked by tst_browsercatalog on the CI
    // runners, whose images include Google Chrome and Microsoft Edge.
    const char *bundleId;
    // Windows program name under the "App Paths" registry key.
    const char *windowsExe;
    // Linux program names, tried in order on PATH.
    QStringList linuxNames;
};

QList<Candidate> candidates()
{
    // Owner decision order: Brave, Google Chrome, (Firefox: not driven, see
    // the header), Microsoft Edge; on Linux also Chromium.
    return {
        {"brave", "Brave", "com.brave.Browser", "brave.exe", {QStringLiteral("brave-browser")}},
        {"chrome", "Google Chrome", "com.google.Chrome", "chrome.exe",
         {QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome")}},
        {"edge", "Microsoft Edge", "com.microsoft.edgemac", "msedge.exe",
         {QStringLiteral("microsoft-edge-stable"), QStringLiteral("microsoft-edge")}},
        {"chromium", "Chromium", nullptr, nullptr, {QStringLiteral("chromium"), QStringLiteral("chromium-browser")}},
    };
}

#if defined(Q_OS_MACOS)
// The executable of the first application Launch Services knows for
// `bundleId`, or an empty string.
QString macExecutable(const char *bundleId)
{
    if (!bundleId)
        return {};
    CFStringRef identifier = CFStringCreateWithCString(kCFAllocatorDefault, bundleId, kCFStringEncodingUTF8);
    CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier(identifier, nullptr);
    CFRelease(identifier);
    QString result;
    if (urls) {
        if (CFArrayGetCount(urls) > 0) {
            const auto appUrl = static_cast<CFURLRef>(CFArrayGetValueAtIndex(urls, 0));
            if (CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, appUrl)) {
                if (CFURLRef executable = CFBundleCopyExecutableURL(bundle)) {
                    char path[4096];
                    if (CFURLGetFileSystemRepresentation(executable, true, reinterpret_cast<UInt8 *>(path), sizeof path))
                        result = QString::fromUtf8(path);
                    CFRelease(executable);
                }
                CFRelease(bundle);
            }
        }
        CFRelease(urls);
    }
    return result;
}
#elif defined(Q_OS_WIN)
// The program path registered under App Paths\<exe> (per user first, then
// per machine), or an empty string. "Default" reads the key's unnamed value.
QString windowsExecutable(const char *exe)
{
    if (!exe)
        return {};
    for (const char *root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        const QSettings key(QStringLiteral("%1\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%2")
                                .arg(QLatin1String(root), QLatin1String(exe)),
                            QSettings::NativeFormat);
        QString path = key.value(QStringLiteral("Default")).toString().trimmed();
        if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
            path = path.mid(1, path.size() - 2);
        if (!path.isEmpty())
            return path;
    }
    return {};
}
#endif

} // namespace

QStringList BrowserCatalog::preferenceOrder()
{
    QStringList order;
    for (const Candidate &candidate : candidates()) {
#if !defined(Q_OS_LINUX)
        // Chromium is part of the order on Linux only (owner decision).
        if (QLatin1String(candidate.id) == QLatin1String("chromium"))
            continue;
#endif
        order << QString::fromLatin1(candidate.id);
    }
    return order;
}

QList<InstalledBrowser> BrowserCatalog::installed()
{
    QList<InstalledBrowser> result;
    const QStringList order = preferenceOrder();
    for (const Candidate &candidate : candidates()) {
        if (!order.contains(QString::fromLatin1(candidate.id)))
            continue;
        QString executable;
#if defined(Q_OS_MACOS)
        executable = macExecutable(candidate.bundleId);
#elif defined(Q_OS_WIN)
        executable = windowsExecutable(candidate.windowsExe);
#else
        for (const QString &name : candidate.linuxNames) {
            executable = QStandardPaths::findExecutable(name);
            if (!executable.isEmpty())
                break;
        }
#endif
        const QFileInfo info(executable);
        if (!executable.isEmpty() && info.isFile() && info.isExecutable())
            result.append({QString::fromLatin1(candidate.id), QString::fromLatin1(candidate.name), executable});
    }
    return result;
}

std::optional<InstalledBrowser> BrowserCatalog::choose(const QList<InstalledBrowser> &installed,
                                                       const QString &preferredId)
{
    for (const InstalledBrowser &browser : installed) {
        if (!preferredId.isEmpty() && browser.id == preferredId)
            return browser;
    }
    if (installed.isEmpty())
        return std::nullopt;
    return installed.first();
}

} // namespace app::browser
