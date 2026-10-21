#include "KeyFiles.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace sellerserver {

namespace sl = app::sellerlist;

namespace {

constexpr qint64 kMaxJsonFile = 4 * 1024 * 1024;

// One line from the terminal with echo off (restored afterwards).
QByteArray readHiddenLine(const char *prompt)
{
    std::fputs(prompt, stderr);
    std::fflush(stderr);
#ifdef Q_OS_WIN
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = GetConsoleMode(input, &mode) != 0;
    if (console)
        SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
    termios old {};
    const bool terminal = tcgetattr(STDIN_FILENO, &old) == 0;
    if (terminal) {
        termios hidden = old;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }
#endif
    QByteArray line;
    for (int c = std::fgetc(stdin); c != EOF && c != '\n'; c = std::fgetc(stdin))
        line.append(static_cast<char>(c));
#ifdef Q_OS_WIN
    if (console)
        SetConsoleMode(input, mode);
#else
    if (terminal)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
#endif
    std::fputs("\n", stderr);
    if (line.endsWith('\r'))
        line.chop(1);
    return line;
}

bool saveSecret(const QString &path, QByteArray blob, const QByteArray &passphrase, QString *error)
{
    const std::optional<QJsonObject> file = sl::encryptSecret(blob, passphrase);
    sl::wipe(&blob);
    if (!file) {
        *error = QStringLiteral("encrypting the key failed");
        return false;
    }
    return writeNewFile(path, QJsonDocument(*file).toJson(), true, error);
}

std::optional<QByteArray> loadSecret(const QString &path, const QByteArray &passphrase, QString *error)
{
    const std::optional<QJsonObject> file = readJsonFile(path, error);
    if (!file)
        return std::nullopt;
    std::optional<QByteArray> blob = sl::decryptSecret(*file, passphrase);
    if (!blob)
        *error = QStringLiteral("%1: wrong passphrase or damaged key file").arg(path);
    return blob;
}

} // namespace

bool writeNewFile(const QString &path, const QByteArray &data, bool secret, QString *error)
{
    QFile file(path);
    const QFileDevice::Permissions permissions = secret
        ? (QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        : (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly, permissions)) {
        *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.flush()) {
        *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        file.close();
        file.remove();
        return false;
    }
    return true;
}

bool replaceFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

std::optional<QJsonObject> readJsonFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    if (file.size() > kMaxJsonFile) {
        *error = QStringLiteral("%1: too large").arg(path);
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        *error = QStringLiteral("%1: not a JSON object (%2)").arg(path, parseError.errorString());
        return std::nullopt;
    }
    return document.object();
}

std::optional<QByteArray> readPassphrase(const QString &path, bool confirm, QString *error)
{
    QByteArray passphrase;
    if (!path.isEmpty()) {
        QFile file(path);
#ifndef Q_OS_WIN
        // Unix permissions (on Windows, Qt does not read NTFS ownership; the
        // file's folder must be protected there).
        if (file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup
                                  | QFileDevice::WriteOther)) {
            *error = QStringLiteral("%1: other users can read or change it (owner only, e.g. chmod 600)").arg(path);
            return std::nullopt;
        }
#endif
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
            return std::nullopt;
        }
        passphrase = file.readLine(4096);
        while (passphrase.endsWith('\n') || passphrase.endsWith('\r'))
            passphrase.chop(1);
    } else {
        passphrase = readHiddenLine("Passphrase: ");
        if (confirm) {
            QByteArray again = readHiddenLine("Passphrase again: ");
            const bool same = again == passphrase;
            sl::wipe(&again);
            if (!same) {
                sl::wipe(&passphrase);
                *error = QStringLiteral("the two passphrases differ");
                return std::nullopt;
            }
        }
    }
    if (QString::fromUtf8(passphrase).size() < kMinPassphraseLength) {
        sl::wipe(&passphrase);
        *error = QStringLiteral("the passphrase must have at least %1 characters").arg(kMinPassphraseLength);
        return std::nullopt;
    }
    return passphrase;
}

bool saveSignKeys(const QString &path, const sl::SignKeys &keys, const QByteArray &passphrase, QString *error)
{
    return saveSecret(path, sl::signKeysBlob(keys), passphrase, error);
}

std::optional<sl::SignKeys> loadSignKeys(const QString &path, const QByteArray &passphrase, QString *error)
{
    std::optional<QByteArray> blob = loadSecret(path, passphrase, error);
    if (!blob)
        return std::nullopt;
    std::optional<sl::SignKeys> keys = sl::signKeysFromBlob(*blob);
    sl::wipe(&*blob);
    if (!keys)
        *error = QStringLiteral("%1: not a signing key").arg(path);
    return keys;
}

bool saveBoxKeys(const QString &path, const sl::BoxKeys &keys, const QByteArray &passphrase, QString *error)
{
    return saveSecret(path, sl::boxKeysBlob(keys), passphrase, error);
}

std::optional<sl::BoxKeys> loadBoxKeys(const QString &path, const QByteArray &passphrase, QString *error)
{
    std::optional<QByteArray> blob = loadSecret(path, passphrase, error);
    if (!blob)
        return std::nullopt;
    std::optional<sl::BoxKeys> keys = sl::boxKeysFromBlob(*blob);
    sl::wipe(&*blob);
    if (!keys)
        *error = QStringLiteral("%1: not an encryption key").arg(path);
    return keys;
}

void wipe(sl::SignKeys *keys)
{
    sl::wipe(&keys->ed25519Secret);
    sl::wipe(&keys->mldsa65Secret);
}

void wipe(sl::BoxKeys *keys)
{
    sl::wipe(&keys->x25519Secret);
    sl::wipe(&keys->mlkem768Secret);
}

} // namespace sellerserver
