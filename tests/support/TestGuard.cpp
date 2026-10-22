// Built into every test and test helper (tests/CMakeLists.txt), before main()
// runs (owner decision 2026-10-22, after the work folder was lost):
//   - Qt's test mode: QStandardPaths gives test folders (macOS:
//     ~/.qttest/Application Support), so a test's default folder is never the
//     user's;
//   - the real and the dev work folder and their backup folders are
//     protected: WorkFolder::open refuses them, SafeDelete deletes nothing
//     there, and writeTestFile() writes nothing there.
#include "core/SafeDelete.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace {

void guardRealData()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString real = app::SafeDelete::realWorkFolder();
    const QString dev = app::SafeDelete::devWorkFolder();
    for (const QString &root : {real, dev, real + QStringLiteral("-Backups"), dev + QStringLiteral("-Backups")})
        app::SafeDelete::forbid(root);
}

} // namespace

Q_CONSTRUCTOR_FUNCTION(guardRealData)
