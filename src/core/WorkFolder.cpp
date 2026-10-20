#include "core/WorkFolder.h"

#include "core/PathChecks.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <utility>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <stdlib.h>
#  include <string>
#endif

namespace app {

namespace {

// Subfolders created inside the work folder, parents before children.
const char *const kSubfolders[] = {
    "backups",
    "logs",
    "browser",
    "tmp",
    "plugins",
};

// The marker holds one UUID and a newline; anything longer is malformed.
constexpr qint64 kMaxMarkerBytes = 64;

std::optional<WorkFolder> failWith(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return std::nullopt;
}

bool readMarker(const QString &fileName, QString *id, QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read the work folder marker: %1").arg(file.errorString());
        return false;
    }
    const QByteArray data = file.read(kMaxMarkerBytes + 1);
    if (data.size() > kMaxMarkerBytes) {
        *error = QStringLiteral("the work folder marker is malformed (too long)");
        return false;
    }
    const QString text = QString::fromLatin1(data).trimmed();
    if (!WorkFolder::isValidMarkerId(text)) {
        *error = QStringLiteral("the work folder marker is malformed");
        return false;
    }
    *id = text;
    return true;
}

bool writeMarker(const QString &fileName, QString *id, QString *error)
{
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("cannot create the work folder marker: %1").arg(file.errorString());
        return false;
    }
    const QByteArray line = newId.toLatin1() + '\n';
    if (file.write(line) != line.size() || !file.commit()) {
        *error = QStringLiteral("cannot write the work folder marker: %1").arg(file.errorString());
        return false;
    }
    *id = newId;
    return true;
}

} // namespace

WorkFolder::WorkFolder(QString path, QString appIdentifier, QString markerId)
    : m_path(std::move(path)), m_appIdentifier(std::move(appIdentifier)), m_markerId(std::move(markerId))
{
}

QString WorkFolder::defaultPath(const QString &appIdentifier)
{
    if (appIdentifier.isEmpty())
        return {};
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        return {};
    return QDir::cleanPath(base + QLatin1Char('/') + appIdentifier);
}

bool WorkFolder::isValidMarkerId(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

std::optional<WorkFolder> WorkFolder::open(const QString &path, const QString &appIdentifier,
                                           QString *error)
{
    if (path.isEmpty() || appIdentifier.isEmpty())
        return failWith(error, QStringLiteral("the work folder path or app name is empty"));
    if (!QDir::isAbsolutePath(path))
        return failWith(error, QStringLiteral("the work folder path is not absolute"));

    const QString root = QDir::cleanPath(path);
    if (isLink(root))
        return failWith(error, QStringLiteral("the work folder is a link; links are never followed"));

    const QString marker = root + QLatin1Char('/') + QLatin1String(kMarkerFileName);
    QString id;
    QString message;

    const QFileInfo rootInfo(root);
    if (rootInfo.exists()) {
        if (!rootInfo.isDir())
            return failWith(error, QStringLiteral("the work folder path exists and is not a folder"));
        if (isLink(marker))
            return failWith(error, QStringLiteral("the work folder marker is a link"));
        if (QFileInfo::exists(marker)) {
            if (!readMarker(marker, &id, &message))
                return failWith(error, message);
        } else {
            const QDir dir(root);
            if (!dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
                return failWith(error, QStringLiteral("the folder exists, has no work folder marker "
                                                      "and is not empty; it is not used"));
        }
    } else if (!QDir().mkpath(root)) {
        return failWith(error, QStringLiteral("cannot create the work folder"));
    }

#ifndef Q_OS_WIN
    // Owner-only access (mode 0700): the folder holds the browser's cookies.
    if (!QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                         | QFileDevice::ExeOwner | QFileDevice::ReadUser
                                         | QFileDevice::WriteUser | QFileDevice::ExeUser))
        return failWith(error, QStringLiteral("cannot restrict the work folder to its owner"));
#endif

    // The marker is written before anything else is created, so a folder the
    // app created always carries it, even if a later step fails.
    if (id.isEmpty() && !writeMarker(marker, &id, &message))
        return failWith(error, message);

    for (const char *sub : kSubfolders) {
        const QString dir = root + QLatin1Char('/') + QLatin1String(sub);
        if (isLink(dir))
            return failWith(error, QStringLiteral("%1 is a link; links are never followed")
                                       .arg(QLatin1String(sub)));
        const QFileInfo info(dir);
        if (info.exists() && !info.isDir())
            return failWith(error, QStringLiteral("%1 exists and is not a folder").arg(QLatin1String(sub)));
        if (!info.exists() && !QDir().mkpath(dir))
            return failWith(error, QStringLiteral("cannot create %1").arg(QLatin1String(sub)));
    }

    // Files other code opens later must not be links either.
    const QString files[] = {root + QStringLiteral("/settings.ini"),
                             root + QStringLiteral("/page-count.json"),
                             root + QStringLiteral("/known-models.json"),
                             root + QLatin1Char('/') + appIdentifier + QStringLiteral(".sqlite"),
                             root + QLatin1Char('/') + QLatin1String(kLockFileName)};
    for (const QString &file : files) {
        if (isLink(file))
            return failWith(error, QStringLiteral("%1 is a link; links are never followed")
                                       .arg(QFileInfo(file).fileName()));
    }

    return WorkFolder(root, appIdentifier, id);
}

bool WorkFolder::redirectTempDirectory(QString *error) const
{
    const QString dir = tmpDir();
#ifdef Q_OS_WIN
    // GetTempPathW reads TMP, then TEMP, from the process environment; the C
    // runtime keeps its own copy for getenv(). Both are set.
    const std::wstring value = QDir::toNativeSeparators(dir).toStdWString();
    if (!SetEnvironmentVariableW(L"TMP", value.c_str()) || !SetEnvironmentVariableW(L"TEMP", value.c_str())
        || _wputenv_s(L"TMP", value.c_str()) != 0 || _wputenv_s(L"TEMP", value.c_str()) != 0) {
        if (error)
            *error = QStringLiteral("cannot set TMP/TEMP");
        return false;
    }
#else
    if (!qputenv("TMPDIR", QFile::encodeName(dir))) {
        if (error)
            *error = QStringLiteral("cannot set TMPDIR");
        return false;
    }
#endif
    return true;
}

QString WorkFolder::markerPath() const { return m_path + QLatin1Char('/') + QLatin1String(kMarkerFileName); }
QString WorkFolder::lockPath() const { return m_path + QLatin1Char('/') + QLatin1String(kLockFileName); }
QString WorkFolder::databasePath() const { return m_path + QLatin1Char('/') + m_appIdentifier + QStringLiteral(".sqlite"); }
QString WorkFolder::backupsDir() const { return m_path + QStringLiteral("/backups"); }
QString WorkFolder::logsDir() const { return m_path + QStringLiteral("/logs"); }
QString WorkFolder::settingsPath() const { return m_path + QStringLiteral("/settings.ini"); }
QString WorkFolder::pageCountPath() const { return m_path + QStringLiteral("/page-count.json"); }
QString WorkFolder::knownModelsPath() const { return m_path + QStringLiteral("/known-models.json"); }
QString WorkFolder::browserProfilesDir() const { return m_path + QStringLiteral("/browser"); }
QString WorkFolder::tmpDir() const { return m_path + QStringLiteral("/tmp"); }
QString WorkFolder::pluginsDir() const { return m_path + QStringLiteral("/plugins"); }

} // namespace app
