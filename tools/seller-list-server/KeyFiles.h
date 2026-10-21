#pragma once

#include "sellerlist/Crypto.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace sellerserver {

// Key and document files of the server and of the owner's master key.
// Secret keys are only ever written encrypted (app::sellerlist::
// encryptSecret: Argon2id + XChaCha20-Poly1305), to a NEW file (never over an
// existing one) readable and writable by its owner only.

inline constexpr int kMinPassphraseLength = 12;

// Writes `data` to a new file; false and *error when the file exists or
// cannot be written. `secret`: permissions owner read/write only.
bool writeNewFile(const QString &path, const QByteArray &data, bool secret, QString *error);
// Replaces a file through a temporary file (public documents only).
bool replaceFile(const QString &path, const QByteArray &data, QString *error);
std::optional<QJsonObject> readJsonFile(const QString &path, QString *error);

// The passphrase: the first line of `file` when given (on Unix a file that
// others can read is refused), else typed at the terminal without echo
// (twice when `confirm`). At least kMinPassphraseLength characters.
std::optional<QByteArray> readPassphrase(const QString &file, bool confirm, QString *error);

bool saveSignKeys(const QString &path, const app::sellerlist::SignKeys &keys, const QByteArray &passphrase, QString *error);
std::optional<app::sellerlist::SignKeys> loadSignKeys(const QString &path, const QByteArray &passphrase, QString *error);
bool saveBoxKeys(const QString &path, const app::sellerlist::BoxKeys &keys, const QByteArray &passphrase, QString *error);
std::optional<app::sellerlist::BoxKeys> loadBoxKeys(const QString &path, const QByteArray &passphrase, QString *error);

// Wipes the secret parts of key pairs.
void wipe(app::sellerlist::SignKeys *keys);
void wipe(app::sellerlist::BoxKeys *keys);

} // namespace sellerserver
