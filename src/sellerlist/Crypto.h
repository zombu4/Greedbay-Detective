#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace app::sellerlist {

// The shared seller list's cryptography (owner decisions 2026-10-20: "the
// best encryption possible", only proven, unmodified standards, hybrid
// post-quantum). Only library constructions are used, unchanged:
//   libsodium 1.0.22: Ed25519 (crypto_sign), X25519 (crypto_scalarmult),
//     XChaCha20-Poly1305-IETF (crypto_aead_xchacha20poly1305_ietf),
//     BLAKE2b (crypto_generichash), Argon2id (crypto_pwhash), randombytes;
//   liboqs 0.16.0: ML-DSA-65 (FIPS 204), ML-KEM-768 (FIPS 203).
//
// Hybrid signatures: every signed document carries an Ed25519 AND an
// ML-DSA-65 signature over the same bytes; verify() accepts only when both
// verify (so it is at least as strong as the stronger of the two).
//
// Hybrid encryption to a recipient's box keys (X25519 + ML-KEM-768): an
// ephemeral X25519 key pair and an ML-KEM-768 encapsulation give two shared
// secrets; the AEAD key is BLAKE2b-256 over a fixed label, both secrets and
// the public values (ephemeral key, recipient key, KEM ciphertext) - as
// libsodium's crypto_scalarmult notes ask: "Hash the result with the public
// keys". The message is sealed with XChaCha20-Poly1305 under a random 192-bit
// nonce (libsodium: "allows random nonces to be safely used"). libsodium's
// own sealed box (X25519 + XSalsa20-Poly1305) cannot take the second secret,
// so this envelope uses the same building blocks, combined as above.
//
// Every function returns std::nullopt / false on any failure; nothing is
// partly decrypted or partly verified.

// Must be called once before anything below (sodium_init); true when the
// libraries are ready.
bool initialize();

struct SignPublic {
    QByteArray ed25519; // 32 bytes
    QByteArray mldsa65; // 1952 bytes
    QJsonObject toJson() const;
    static std::optional<SignPublic> fromJson(const QJsonObject &object);
    bool operator==(const SignPublic &other) const = default;
};

struct SignKeys {
    SignPublic pub;
    QByteArray ed25519Secret; // 64 bytes
    QByteArray mldsa65Secret; // 4032 bytes
};

struct BoxPublic {
    QByteArray x25519;    // 32 bytes
    QByteArray mlkem768;  // 1184 bytes
    QJsonObject toJson() const;
    static std::optional<BoxPublic> fromJson(const QJsonObject &object);
    bool operator==(const BoxPublic &other) const = default;
};

struct BoxKeys {
    BoxPublic pub;
    QByteArray x25519Secret;   // 32 bytes
    QByteArray mlkem768Secret; // 2400 bytes
};

struct HybridSignature {
    QByteArray ed25519; // 64 bytes
    QByteArray mldsa65; // 3309 bytes
    QJsonObject toJson() const;
    static std::optional<HybridSignature> fromJson(const QJsonObject &object);
};

std::optional<SignKeys> newSignKeys();

// The installation's own report key (Ed25519 only: it signs reports for the
// server's abuse limits, it protects no secret). Made once per installation.
struct InstallKey {
    QByteArray pub;    // 32 bytes
    QByteArray secret; // 64 bytes
};
std::optional<InstallKey> newInstallKey();
// The Ed25519 signature of `message`, empty on failure.
QByteArray installSign(const QByteArray &message, const InstallKey &key);
std::optional<BoxKeys> newBoxKeys();

std::optional<HybridSignature> sign(const QByteArray &message, const SignKeys &keys);
bool verify(const QByteArray &message, const HybridSignature &signature, const SignPublic &key);

// The envelope: version byte 1 | ephemeral X25519 key (32) | ML-KEM-768
// ciphertext (1088) | nonce (24) | AEAD ciphertext. `label` binds the use
// ("report", "dispute") into the key and the AEAD's associated data.
std::optional<QByteArray> seal(const QByteArray &message, const BoxPublic &to, const QString &label);
std::optional<QByteArray> open(const QByteArray &envelope, const BoxKeys &keys, const QString &label);

// Secret keys at rest: XChaCha20-Poly1305 under a key from the passphrase
// (Argon2id, libsodium's MODERATE limits: 3 passes, 256 MiB), a random salt
// and nonce. The result is a JSON object (format 1) holding everything but
// the passphrase.
std::optional<QJsonObject> encryptSecret(const QByteArray &secret, const QByteArray &passphrase);
std::optional<QByteArray> decryptSecret(const QJsonObject &file, const QByteArray &passphrase);

// Key pairs as one secret blob (for encryptSecret) and back.
QByteArray signKeysBlob(const SignKeys &keys);
std::optional<SignKeys> signKeysFromBlob(const QByteArray &blob);
QByteArray boxKeysBlob(const BoxKeys &keys);
std::optional<BoxKeys> boxKeysFromBlob(const QByteArray &blob);

// Wipes a secret (sodium_memzero).
void wipe(QByteArray *secret);

// Hex of BLAKE2b-256 of the data: short ids of keys and reports.
QString fingerprint(const QByteArray &data);

// Random bytes (randombytes_buf).
QByteArray randomBytes(int size);

} // namespace app::sellerlist
