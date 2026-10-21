#pragma once

#include "sellerlist/Crypto.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace app::sellerlist {

// The shared seller list's documents (DESIGN 3.7). Every signed document is
// {"body": {...}, "sig": {"ed25519": b64, "mldsa65": b64}}; the signed bytes
// are the body as compact JSON (QJsonDocument::Compact, keys in Qt's sorted
// order; bodies hold only strings, booleans and whole numbers).
//
// Trust chain: the owner's MASTER key (kept on the owner's computer) signs
//   - an Authorization: the server's sign and box keys, its host name, a
//     sequence number and a validity period;
//   - a Revocation: a server sign key that is no longer valid (any list it
//     signs is refused).
// The app ships the master's public keys and the server's host name
// (MasterInfo); it trusts a server key only through a valid, unrevoked
// authorization, and a list only when that key signed it and its sequence
// is newer than the one it has (no rollback).

// Who the app trusts: the master's public sign keys and the server's host
// name (data/master.json in the app; empty until the owner sets it up). The
// app reaches the server at https://<host>/ only.
struct MasterInfo {
    SignPublic master;
    QString host; // a DNS name, e.g. "list.example.org"
    static std::optional<MasterInfo> fromJson(const QJsonObject &object);
};

// A DNS host name the app may use for the server: lower case, 4 to 253
// characters, at least two labels of 1 to 63 characters [a-z0-9-] that do
// not start or end with '-', the last label letters only (so never an IP
// address), no trailing dot.
bool isServerHost(const QString &host);

// Sign / check a document.
std::optional<QJsonObject> signDocument(const QJsonObject &body, const SignKeys &keys);
// The body when the signature verifies with `key`, else std::nullopt.
std::optional<QJsonObject> verifiedBody(const QJsonObject &document, const SignPublic &key);

struct Authorization {
    SignPublic serverSign;
    BoxPublic serverBox;
    QString host;
    qint64 sequence = 0;
    QDateTime validFrom; // UTC
    QDateTime validUntil;
    QJsonObject toBody() const;
    static std::optional<Authorization> fromBody(const QJsonObject &body);
    bool validAt(const QDateTime &time) const { return time >= validFrom && time < validUntil; }
};

struct Revocation {
    SignPublic serverSign;
    qint64 sequence = 0;
    QDateTime revokedAt;
    QJsonObject toBody() const;
    static std::optional<Revocation> fromBody(const QJsonObject &body);
};

// One published report. No reporter identity: no eBay name, no install key,
// and no item number (the seller could find the buyer in their own orders);
// the item number stays on the server, for its item check.
struct PublishedReport {
    QString id;     // hex, from the report's own fingerprint
    QString reason; // a lookup::kMarkReasons id
    QString text;   // the reporter's own words, at most kMaxReportText
    QString month;  // "2026-10": when the reporter bought
    // The server found the item on eBay's item page, sold by this seller.
    bool itemChecked = false;
    QJsonObject toJson() const;
    static std::optional<PublishedReport> fromJson(const QJsonObject &object);
};

struct ListedSeller {
    QString sellerId;
    QList<PublishedReport> reports;
};

struct SellerList {
    qint64 sequence = 0;
    QDateTime signedAt;
    qint64 authorizationSequence = 0; // the authorization of the key that signed it
    QList<ListedSeller> sellers;
    QJsonObject toBody() const;
    static std::optional<SellerList> fromBody(const QJsonObject &body);
    // The seller's reports (case-insensitive id), or nullptr.
    const ListedSeller *find(const QString &sellerId) const;
};

// What the app sends (sealed to the server's box key, label "report"). The
// install key is an Ed25519 key made by the app for this one installation,
// not tied to the person; the server uses it only for its abuse limits
// (one report per installation and seller, a daily cap) and never
// publishes it.
inline constexpr int kMaxReportText = 500;
struct Report {
    QString sellerId;
    QString reason;
    QString text;
    QString itemId;
    QString month;
    QDateTime createdAt;
    QByteArray installKey;       // Ed25519 public key, 32 bytes
    QByteArray installSignature; // Ed25519 over the body without "installSignature"
    QJsonObject toJson() const;
    static std::optional<Report> fromJson(const QJsonObject &object);
    // The bytes the install key signs.
    QByteArray signedBytes() const;
    // Everything the server checks without the network: the fields' forms,
    // the text's length and characters, the install key's signature.
    // Empty when valid, else why not.
    QString problem() const;
};

// A dispute of a published report (or of a seller's listing as a whole):
// anyone may send one; it goes to the owner's queue only (label "dispute").
inline constexpr int kMaxDisputeText = 1000;
struct Dispute {
    QString sellerId;
    QString reportId; // may be empty (about the seller's listing)
    QString text;
    QJsonObject toJson() const;
    static std::optional<Dispute> fromJson(const QJsonObject &object);
    QString problem() const;
};

// Text a reporter wrote: no control characters except line breaks, no
// markup is interpreted anywhere (shown as plain text), 1..max characters
// after trimming.
bool isCleanText(const QString &text, int maxLength);

} // namespace app::sellerlist
