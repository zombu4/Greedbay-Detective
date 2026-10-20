#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace app {

// Exports (DESIGN 8, PLAN 18): files the user asks for are saved in the
// user's Downloads folder (QStandardPaths::DownloadLocation). No file dialog
// is used, so no dialog keeps a last folder or recent places anywhere
// outside the work folder (work cycle rule 8). The export files are the
// user's own; uninstall does not remove them.

// The Downloads folder; empty when the system reports none.
QString exportFolder();

// Saves `data` as `fileName` in `folder`. `fileName` is a plain name (no
// folder part, no ".."). An existing file is never replaced: "name-2.ext",
// "name-3.ext", ... are tried (up to 999). The path written; std::nullopt
// (and *error) otherwise.
std::optional<QString> saveExport(const QString &folder, const QString &fileName, const QByteArray &data, QString *error);

} // namespace app
