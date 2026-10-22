#pragma once

#include <QString>
#include <QStringList>

namespace app {

// Permission to delete inside the real work folder (owner decision
// 2026-10-22, after the work folder was lost). Only two places make one:
// the in-app Uninstall after the user confirmed in its dialog
// (Backend::confirmUninstall), and the app's --uninstall-data run, which the
// Windows uninstaller starts after its own dialog asked. Sign out of eBay
// gets a narrower one: the browser profile folders only.
class DeleteGrant {
public:
    enum class Scope {
        Everything, // the whole work folder (uninstall)
        BrowserProfiles, // <work>/browser/<id> and its macOS cache twin only (sign out)
    };
    Scope scope() const { return m_scope; }

private:
    explicit DeleteGrant(Scope scope) : m_scope(scope) {}
    Scope m_scope;

    friend class DeleteGrants;
};

// The makers of grants; each is called from exactly one confirmed place.
class DeleteGrants {
public:
    static DeleteGrant confirmedUninstall();
    static DeleteGrant confirmedSignOut();
};

// The one owner of every recursive delete in the app.
//
// removeTree() deletes `path` with everything inside it, never following a
// link (a link is removed as a link). It refuses, touching nothing:
//   - an empty or relative path;
//   - a path inside any root given to forbid() (tests and tools register the
//     real and the dev work folder; nothing there is ever deleted);
//   - the real work folder or anything inside it, unless `grant` is given
//     and covers it (Everything, or BrowserProfiles for paths under
//     <work>/browser/).
// The real work folder is <GenericDataLocation>/<release identifier>, worked
// out with Qt's test mode off (so a test still knows where the real one is).
namespace SafeDelete {

// The real and the dev work folder of this product (absolute, clean).
QString realWorkFolder();
QString devWorkFolder();

// True when `path` is `root` or inside it (clean paths compared, no links
// resolved).
bool isInside(const QString &path, const QString &root);

// Tests and tools: never delete or open anything inside `root`.
void forbid(const QString &root);
// The roots given to forbid().
QStringList forbiddenRoots();

// Empty when `path` may be deleted with `grant` (nullptr: no grant), else why
// not.
QString refusal(const QString &path, const DeleteGrant *grant);
// The same rules with `realRoot` standing for the real work folder (tests).
QString refusalFor(const QString &path, const DeleteGrant *grant, const QString &realRoot);

// Deletes `path` (see above). False and *failed (one line per entry that
// could not go, or the refusal) when not everything went; true when `path` is
// gone (also when it was not there).
bool removeTree(const QString &path, const DeleteGrant *grant, QStringList *failed);

} // namespace SafeDelete

} // namespace app
