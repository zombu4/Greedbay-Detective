#include "sellerlist/Crypto.h"

#include <oqs/oqs.h>
#include <sodium.h>

#include <QJsonDocument>

#include <memory>

namespace app::sellerlist {

namespace {

constexpr int kEdPublic = crypto_sign_ed25519_PUBLICKEYBYTES;
constexpr int kEdSecret = crypto_sign_ed25519_SECRETKEYBYTES;
constexpr int kEdSignature = crypto_sign_ed25519_BYTES;
constexpr int kXKey = crypto_scalarmult_curve25519_BYTES;
constexpr int kNonce = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr int kAeadKey = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr int kAeadTag = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr char kEnvelopeVersion = 1;

struct SigDeleter {
    void operator()(OQS_SIG *sig) const { OQS_SIG_free(sig); }
};
struct KemDeleter {
    void operator()(OQS_KEM *kem) const { OQS_KEM_free(kem); }
};
using Sig = std::unique_ptr<OQS_SIG, SigDeleter>;
using Kem = std::unique_ptr<OQS_KEM, KemDeleter>;

Sig mldsa() { return Sig(OQS_SIG_new(OQS_SIG_alg_ml_dsa_65)); }
Kem mlkem() { return Kem(OQS_KEM_new(OQS_KEM_alg_ml_kem_768)); }

unsigned char *bytes(QByteArray &array) { return reinterpret_cast<unsigned char *>(array.data()); }
const unsigned char *bytes(const QByteArray &array) { return reinterpret_cast<const unsigned char *>(array.constData()); }

QString b64(const QByteArray &data) { return QString::fromLatin1(data.toBase64()); }

std::optional<QByteArray> fromB64(const QJsonValue &value, qsizetype size)
{
    if (!value.isString())
        return std::nullopt;
    const QByteArray::FromBase64Result result =
        QByteArray::fromBase64Encoding(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.size() != size)
        return std::nullopt;
    return result.decoded;
}

// BLAKE2b-256 over the label and the parts, for the hybrid envelope's key.
QByteArray combinedKey(const QString &label, const QByteArray &x25519Secret, const QByteArray &kemSecret,
                       const QByteArray &ephemeral, const QByteArray &recipient, const QByteArray &kemCiphertext)
{
    QByteArray key(kAeadKey, '\0');
    crypto_generichash_state state;
    crypto_generichash_init(&state, nullptr, 0, kAeadKey);
    const QByteArray tag = QByteArrayLiteral("greedbay-detective sellerlist v1 ") + label.toUtf8();
    for (const QByteArray *part : {&tag, &x25519Secret, &kemSecret, &ephemeral, &recipient, &kemCiphertext})
        crypto_generichash_update(&state, bytes(*part), static_cast<unsigned long long>(part->size()));
    crypto_generichash_final(&state, bytes(key), kAeadKey);
    return key;
}

} // namespace

bool initialize()
{
    if (sodium_init() < 0)
        return false;
    OQS_init();
    return mldsa() != nullptr && mlkem() != nullptr;
}

QJsonObject SignPublic::toJson() const
{
    return QJsonObject{{QStringLiteral("ed25519"), b64(ed25519)}, {QStringLiteral("mldsa65"), b64(mldsa65)}};
}

std::optional<SignPublic> SignPublic::fromJson(const QJsonObject &object)
{
    const Sig sig = mldsa();
    if (!sig)
        return std::nullopt;
    const std::optional<QByteArray> ed = fromB64(object.value(QStringLiteral("ed25519")), kEdPublic);
    const std::optional<QByteArray> pq = fromB64(object.value(QStringLiteral("mldsa65")), static_cast<qsizetype>(sig->length_public_key));
    if (!ed || !pq)
        return std::nullopt;
    return SignPublic{*ed, *pq};
}

QJsonObject BoxPublic::toJson() const
{
    return QJsonObject{{QStringLiteral("x25519"), b64(x25519)}, {QStringLiteral("mlkem768"), b64(mlkem768)}};
}

std::optional<BoxPublic> BoxPublic::fromJson(const QJsonObject &object)
{
    const Kem kem = mlkem();
    if (!kem)
        return std::nullopt;
    const std::optional<QByteArray> x = fromB64(object.value(QStringLiteral("x25519")), kXKey);
    const std::optional<QByteArray> pq = fromB64(object.value(QStringLiteral("mlkem768")), static_cast<qsizetype>(kem->length_public_key));
    if (!x || !pq)
        return std::nullopt;
    return BoxPublic{*x, *pq};
}

QJsonObject HybridSignature::toJson() const
{
    return QJsonObject{{QStringLiteral("ed25519"), b64(ed25519)}, {QStringLiteral("mldsa65"), b64(mldsa65)}};
}

std::optional<HybridSignature> HybridSignature::fromJson(const QJsonObject &object)
{
    const Sig sig = mldsa();
    if (!sig)
        return std::nullopt;
    const std::optional<QByteArray> ed = fromB64(object.value(QStringLiteral("ed25519")), kEdSignature);
    const QJsonValue pqValue = object.value(QStringLiteral("mldsa65"));
    if (!ed || !pqValue.isString())
        return std::nullopt;
    const QByteArray::FromBase64Result pq =
        QByteArray::fromBase64Encoding(pqValue.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    // ML-DSA signatures may be shorter than the maximum (liboqs sig.h).
    if (!pq || pq.decoded.isEmpty() || pq.decoded.size() > static_cast<qsizetype>(sig->length_signature))
        return std::nullopt;
    return HybridSignature{*ed, pq.decoded};
}

std::optional<SignKeys> newSignKeys()
{
    const Sig sig = mldsa();
    if (!sig)
        return std::nullopt;
    SignKeys keys;
    keys.pub.ed25519 = QByteArray(kEdPublic, '\0');
    keys.ed25519Secret = QByteArray(kEdSecret, '\0');
    if (crypto_sign_ed25519_keypair(bytes(keys.pub.ed25519), bytes(keys.ed25519Secret)) != 0)
        return std::nullopt;
    keys.pub.mldsa65 = QByteArray(static_cast<qsizetype>(sig->length_public_key), '\0');
    keys.mldsa65Secret = QByteArray(static_cast<qsizetype>(sig->length_secret_key), '\0');
    if (OQS_SIG_keypair(sig.get(), bytes(keys.pub.mldsa65), bytes(keys.mldsa65Secret)) != OQS_SUCCESS)
        return std::nullopt;
    return keys;
}

std::optional<InstallKey> newInstallKey()
{
    InstallKey key{QByteArray(kEdPublic, '\0'), QByteArray(kEdSecret, '\0')};
    if (crypto_sign_ed25519_keypair(bytes(key.pub), bytes(key.secret)) != 0)
        return std::nullopt;
    return key;
}

QByteArray installSign(const QByteArray &message, const InstallKey &key)
{
    if (key.pub.size() != kEdPublic || key.secret.size() != kEdSecret)
        return {};
    QByteArray signature(crypto_sign_ed25519_BYTES, '\0');
    if (crypto_sign_ed25519_detached(bytes(signature), nullptr, bytes(message), static_cast<unsigned long long>(message.size()),
                                     bytes(key.secret))
        != 0)
        return {};
    return signature;
}

std::optional<BoxKeys> newBoxKeys()
{
    const Kem kem = mlkem();
    if (!kem)
        return std::nullopt;
    BoxKeys keys;
    keys.pub.x25519 = QByteArray(kXKey, '\0');
    keys.x25519Secret = QByteArray(kXKey, '\0');
    randombytes_buf(bytes(keys.x25519Secret), kXKey);
    if (crypto_scalarmult_curve25519_base(bytes(keys.pub.x25519), bytes(keys.x25519Secret)) != 0)
        return std::nullopt;
    keys.pub.mlkem768 = QByteArray(static_cast<qsizetype>(kem->length_public_key), '\0');
    keys.mlkem768Secret = QByteArray(static_cast<qsizetype>(kem->length_secret_key), '\0');
    if (OQS_KEM_keypair(kem.get(), bytes(keys.pub.mlkem768), bytes(keys.mlkem768Secret)) != OQS_SUCCESS)
        return std::nullopt;
    return keys;
}

std::optional<HybridSignature> sign(const QByteArray &message, const SignKeys &keys)
{
    const Sig sig = mldsa();
    if (!sig || keys.ed25519Secret.size() != kEdSecret || keys.mldsa65Secret.size() != static_cast<qsizetype>(sig->length_secret_key))
        return std::nullopt;
    HybridSignature signature;
    signature.ed25519 = QByteArray(kEdSignature, '\0');
    if (crypto_sign_ed25519_detached(bytes(signature.ed25519), nullptr, bytes(message),
                                     static_cast<unsigned long long>(message.size()), bytes(keys.ed25519Secret)) != 0)
        return std::nullopt;
    signature.mldsa65 = QByteArray(static_cast<qsizetype>(sig->length_signature), '\0');
    size_t length = 0;
    if (OQS_SIG_sign(sig.get(), bytes(signature.mldsa65), &length, bytes(message), static_cast<size_t>(message.size()),
                     bytes(keys.mldsa65Secret)) != OQS_SUCCESS)
        return std::nullopt;
    signature.mldsa65.truncate(static_cast<qsizetype>(length));
    return signature;
}

bool verify(const QByteArray &message, const HybridSignature &signature, const SignPublic &key)
{
    const Sig sig = mldsa();
    if (!sig || signature.ed25519.size() != kEdSignature || key.ed25519.size() != kEdPublic
        || key.mldsa65.size() != static_cast<qsizetype>(sig->length_public_key) || signature.mldsa65.isEmpty())
        return false;
    // Both halves must verify.
    const bool classic = crypto_sign_ed25519_verify_detached(bytes(signature.ed25519), bytes(message),
                                                             static_cast<unsigned long long>(message.size()), bytes(key.ed25519)) == 0;
    const bool postQuantum = OQS_SIG_verify(sig.get(), bytes(message), static_cast<size_t>(message.size()), bytes(signature.mldsa65),
                                            static_cast<size_t>(signature.mldsa65.size()), bytes(key.mldsa65)) == OQS_SUCCESS;
    return classic && postQuantum;
}

std::optional<QByteArray> seal(const QByteArray &message, const BoxPublic &to, const QString &label)
{
    const Kem kem = mlkem();
    if (!kem || to.x25519.size() != kXKey || to.mlkem768.size() != static_cast<qsizetype>(kem->length_public_key))
        return std::nullopt;
    // X25519 with an ephemeral key.
    QByteArray ephemeralSecret(kXKey, '\0');
    QByteArray ephemeral(kXKey, '\0');
    randombytes_buf(bytes(ephemeralSecret), kXKey);
    QByteArray x25519Secret(kXKey, '\0');
    const bool xOk = crypto_scalarmult_curve25519_base(bytes(ephemeral), bytes(ephemeralSecret)) == 0
                     && crypto_scalarmult_curve25519(bytes(x25519Secret), bytes(ephemeralSecret), bytes(to.x25519)) == 0;
    wipe(&ephemeralSecret);
    if (!xOk)
        return std::nullopt;
    // ML-KEM-768 encapsulation.
    QByteArray kemCiphertext(static_cast<qsizetype>(kem->length_ciphertext), '\0');
    QByteArray kemSecret(static_cast<qsizetype>(kem->length_shared_secret), '\0');
    if (OQS_KEM_encaps(kem.get(), bytes(kemCiphertext), bytes(kemSecret), bytes(to.mlkem768)) != OQS_SUCCESS) {
        wipe(&x25519Secret);
        return std::nullopt;
    }
    QByteArray key = combinedKey(label, x25519Secret, kemSecret, ephemeral, to.x25519, kemCiphertext);
    wipe(&x25519Secret);
    wipe(&kemSecret);
    const QByteArray nonce = randomBytes(kNonce);
    const QByteArray header = QByteArray(1, kEnvelopeVersion) + ephemeral + kemCiphertext + nonce;
    const QByteArray associated = label.toUtf8();
    QByteArray cipher(message.size() + kAeadTag, '\0');
    unsigned long long cipherLength = 0;
    const int sealed = crypto_aead_xchacha20poly1305_ietf_encrypt(
        bytes(cipher), &cipherLength, bytes(message), static_cast<unsigned long long>(message.size()), bytes(associated),
        static_cast<unsigned long long>(associated.size()), nullptr, bytes(nonce), bytes(key));
    wipe(&key);
    if (sealed != 0)
        return std::nullopt;
    cipher.truncate(static_cast<qsizetype>(cipherLength));
    return header + cipher;
}

std::optional<QByteArray> open(const QByteArray &envelope, const BoxKeys &keys, const QString &label)
{
    const Kem kem = mlkem();
    if (!kem || keys.x25519Secret.size() != kXKey || keys.mlkem768Secret.size() != static_cast<qsizetype>(kem->length_secret_key))
        return std::nullopt;
    const qsizetype kemLength = static_cast<qsizetype>(kem->length_ciphertext);
    const qsizetype headerLength = 1 + kXKey + kemLength + kNonce;
    if (envelope.size() < headerLength + kAeadTag || envelope.at(0) != kEnvelopeVersion)
        return std::nullopt;
    const QByteArray ephemeral = envelope.mid(1, kXKey);
    const QByteArray kemCiphertext = envelope.mid(1 + kXKey, kemLength);
    const QByteArray nonce = envelope.mid(1 + kXKey + kemLength, kNonce);
    const QByteArray cipher = envelope.mid(headerLength);
    QByteArray x25519Secret(kXKey, '\0');
    if (crypto_scalarmult_curve25519(bytes(x25519Secret), bytes(keys.x25519Secret), bytes(ephemeral)) != 0)
        return std::nullopt;
    QByteArray kemSecret(static_cast<qsizetype>(kem->length_shared_secret), '\0');
    if (OQS_KEM_decaps(kem.get(), bytes(kemSecret), bytes(kemCiphertext), bytes(keys.mlkem768Secret)) != OQS_SUCCESS) {
        wipe(&x25519Secret);
        return std::nullopt;
    }
    QByteArray key = combinedKey(label, x25519Secret, kemSecret, ephemeral, keys.pub.x25519, kemCiphertext);
    wipe(&x25519Secret);
    wipe(&kemSecret);
    const QByteArray associated = label.toUtf8();
    QByteArray message(cipher.size(), '\0');
    unsigned long long messageLength = 0;
    const int opened = crypto_aead_xchacha20poly1305_ietf_decrypt(
        bytes(message), &messageLength, nullptr, bytes(cipher), static_cast<unsigned long long>(cipher.size()), bytes(associated),
        static_cast<unsigned long long>(associated.size()), bytes(nonce), bytes(key));
    wipe(&key);
    if (opened != 0)
        return std::nullopt;
    message.truncate(static_cast<qsizetype>(messageLength));
    return message;
}

std::optional<QJsonObject> encryptSecret(const QByteArray &secret, const QByteArray &passphrase)
{
    if (passphrase.isEmpty())
        return std::nullopt;
    const QByteArray salt = randomBytes(crypto_pwhash_SALTBYTES);
    const QByteArray nonce = randomBytes(kNonce);
    QByteArray key(kAeadKey, '\0');
    if (crypto_pwhash(bytes(key), kAeadKey, passphrase.constData(), static_cast<unsigned long long>(passphrase.size()), bytes(salt),
                      crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE, crypto_pwhash_ALG_ARGON2ID13) != 0)
        return std::nullopt;
    QByteArray cipher(secret.size() + kAeadTag, '\0');
    unsigned long long cipherLength = 0;
    const QByteArray associated = QByteArrayLiteral("greedbay-detective secret v1");
    const int sealed = crypto_aead_xchacha20poly1305_ietf_encrypt(bytes(cipher), &cipherLength, bytes(secret),
                                                                  static_cast<unsigned long long>(secret.size()), bytes(associated),
                                                                  static_cast<unsigned long long>(associated.size()), nullptr,
                                                                  bytes(nonce), bytes(key));
    wipe(&key);
    if (sealed != 0)
        return std::nullopt;
    cipher.truncate(static_cast<qsizetype>(cipherLength));
    return QJsonObject{{QStringLiteral("format"), 1},
                       {QStringLiteral("kdf"), QStringLiteral("argon2id13")},
                       {QStringLiteral("opslimit"), static_cast<double>(crypto_pwhash_OPSLIMIT_MODERATE)},
                       {QStringLiteral("memlimit"), static_cast<double>(crypto_pwhash_MEMLIMIT_MODERATE)},
                       {QStringLiteral("salt"), b64(salt)},
                       {QStringLiteral("nonce"), b64(nonce)},
                       {QStringLiteral("cipher"), QStringLiteral("xchacha20poly1305-ietf")},
                       {QStringLiteral("data"), b64(cipher)}};
}

std::optional<QByteArray> decryptSecret(const QJsonObject &file, const QByteArray &passphrase)
{
    if (file.value(QStringLiteral("format")).toInt() != 1 || file.value(QStringLiteral("kdf")).toString() != QLatin1String("argon2id13")
        || file.value(QStringLiteral("cipher")).toString() != QLatin1String("xchacha20poly1305-ietf"))
        return std::nullopt;
    const std::optional<QByteArray> salt = fromB64(file.value(QStringLiteral("salt")), crypto_pwhash_SALTBYTES);
    const std::optional<QByteArray> nonce = fromB64(file.value(QStringLiteral("nonce")), kNonce);
    const QByteArray::FromBase64Result data = QByteArray::fromBase64Encoding(
        file.value(QStringLiteral("data")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const double ops = file.value(QStringLiteral("opslimit")).toDouble();
    const double mem = file.value(QStringLiteral("memlimit")).toDouble();
    // Only the limits this format writes are accepted (no downgrade).
    if (!salt || !nonce || !data || data.decoded.size() < kAeadTag || ops != static_cast<double>(crypto_pwhash_OPSLIMIT_MODERATE)
        || mem != static_cast<double>(crypto_pwhash_MEMLIMIT_MODERATE))
        return std::nullopt;
    QByteArray key(kAeadKey, '\0');
    if (crypto_pwhash(bytes(key), kAeadKey, passphrase.constData(), static_cast<unsigned long long>(passphrase.size()), bytes(*salt),
                      crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE, crypto_pwhash_ALG_ARGON2ID13) != 0)
        return std::nullopt;
    const QByteArray associated = QByteArrayLiteral("greedbay-detective secret v1");
    QByteArray secret(data.decoded.size(), '\0');
    unsigned long long secretLength = 0;
    const int opened = crypto_aead_xchacha20poly1305_ietf_decrypt(
        bytes(secret), &secretLength, nullptr, bytes(data.decoded), static_cast<unsigned long long>(data.decoded.size()),
        bytes(associated), static_cast<unsigned long long>(associated.size()), bytes(*nonce), bytes(key));
    wipe(&key);
    if (opened != 0) {
        wipe(&secret);
        return std::nullopt;
    }
    secret.truncate(static_cast<qsizetype>(secretLength));
    return secret;
}

QByteArray signKeysBlob(const SignKeys &keys)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("public"), keys.pub.toJson()},
                                     {QStringLiteral("ed25519"), b64(keys.ed25519Secret)},
                                     {QStringLiteral("mldsa65"), b64(keys.mldsa65Secret)}})
        .toJson(QJsonDocument::Compact);
}

std::optional<SignKeys> signKeysFromBlob(const QByteArray &blob)
{
    const Sig sig = mldsa();
    const QJsonObject object = QJsonDocument::fromJson(blob).object();
    const std::optional<SignPublic> pub = SignPublic::fromJson(object.value(QStringLiteral("public")).toObject());
    if (!sig || !pub)
        return std::nullopt;
    const std::optional<QByteArray> ed = fromB64(object.value(QStringLiteral("ed25519")), kEdSecret);
    const std::optional<QByteArray> pq = fromB64(object.value(QStringLiteral("mldsa65")), static_cast<qsizetype>(sig->length_secret_key));
    if (!ed || !pq)
        return std::nullopt;
    return SignKeys{*pub, *ed, *pq};
}

QByteArray boxKeysBlob(const BoxKeys &keys)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("public"), keys.pub.toJson()},
                                     {QStringLiteral("x25519"), b64(keys.x25519Secret)},
                                     {QStringLiteral("mlkem768"), b64(keys.mlkem768Secret)}})
        .toJson(QJsonDocument::Compact);
}

std::optional<BoxKeys> boxKeysFromBlob(const QByteArray &blob)
{
    const Kem kem = mlkem();
    const QJsonObject object = QJsonDocument::fromJson(blob).object();
    const std::optional<BoxPublic> pub = BoxPublic::fromJson(object.value(QStringLiteral("public")).toObject());
    if (!kem || !pub)
        return std::nullopt;
    const std::optional<QByteArray> x = fromB64(object.value(QStringLiteral("x25519")), kXKey);
    const std::optional<QByteArray> pq = fromB64(object.value(QStringLiteral("mlkem768")), static_cast<qsizetype>(kem->length_secret_key));
    if (!x || !pq)
        return std::nullopt;
    return BoxKeys{*pub, *x, *pq};
}

void wipe(QByteArray *secret)
{
    if (!secret->isEmpty())
        sodium_memzero(secret->data(), static_cast<size_t>(secret->size()));
    secret->clear();
}

QString fingerprint(const QByteArray &data)
{
    QByteArray hash(32, '\0');
    crypto_generichash(bytes(hash), 32, bytes(data), static_cast<unsigned long long>(data.size()), nullptr, 0);
    return QString::fromLatin1(hash.toHex());
}

QByteArray randomBytes(int size)
{
    QByteArray data(size, '\0');
    randombytes_buf(bytes(data), static_cast<size_t>(size));
    return data;
}

} // namespace app::sellerlist
