#include "core/ExportFiles.h"

#include "core/PathChecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace app {

QString exportFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

std::optional<QString> saveExport(const QString &folder, const QString &fileName, const QByteArray &data, QString *error)
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        *error = QStringLiteral("there is no Downloads folder to save into");
        return std::nullopt;
    }
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
        || fileName.contains(QStringLiteral("..")) || fileName.startsWith(QLatin1Char('.'))) {
        *error = QStringLiteral("\"%1\" is not a plain file name").arg(fileName);
        return std::nullopt;
    }
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString extension = dot > 0 ? fileName.mid(dot) : QString();
    // Names Windows reserves for devices: "CON, PRN, AUX, NUL, COM1 ... COM9,
    // COM¹, COM², COM³, LPT1 ... LPT9, LPT¹, LPT², and LPT³. Also avoid these
    // names followed immediately by an extension; for example, NUL.txt and
    // NUL.tar.gz are both equivalent to NUL."
    // (https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)
    static const QRegularExpression device(
        QStringLiteral("^(con|prn|aux|nul|com[1-9\\x{00B9}\\x{00B2}\\x{00B3}]|lpt[1-9\\x{00B9}\\x{00B2}\\x{00B3}])$"),
        QRegularExpression::CaseInsensitiveOption);
    if (device.match(stem.section(QLatin1Char('.'), 0, 0)).hasMatch()) {
        *error = QStringLiteral("\"%1\" is a device name on Windows").arg(fileName);
        return std::nullopt;
    }
    for (int n = 1; n <= 999; ++n) {
        const QString name = n == 1 ? fileName : QStringLiteral("%1-%2%3").arg(stem).arg(n).arg(extension);
        const QString path = QDir(folder).filePath(name);
        if (somethingAt(path))
            continue;
        QFile file(path);
        // NewOnly: fails when the file appeared in the meantime; never replaces.
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (somethingAt(path))
                continue;
            *error = QStringLiteral("%1 cannot be created: %2").arg(QDir::toNativeSeparators(path), file.errorString());
            return std::nullopt;
        }
        if (file.write(data) != data.size() || !file.flush()) {
            const QString reason = file.errorString();
            file.close();
            file.remove();
            *error = QStringLiteral("%1 cannot be written: %2").arg(QDir::toNativeSeparators(path), reason);
            return std::nullopt;
        }
        file.close();
        return path;
    }
    *error = QStringLiteral("there are already 999 files named like %1").arg(fileName);
    return std::nullopt;
}

} // namespace app
