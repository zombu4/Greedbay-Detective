#pragma once

#include "sellerlist/Protocol.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace app::sellerlist {

// What the server serves at GET /v1/list (one HTTPS request):
//   {"authorization": <master-signed Authorization>,
//    "revocations": [<master-signed Revocation>, ...],
//    "list": <server-signed SellerList>}
// checkBundle() decides whether the app may use the list:
//   1 the authorization is signed by the master (MasterInfo), names the
//     host name the app knows, and is valid now;
//   2 no master-signed revocation names the authorization's server key;
//   3 the list is signed by that server key, names that authorization's
//     sequence, and its sequence is greater than `lastSequence` (the last
//     list the app accepted; no rollback, no replay);
//   4 its time is not in the future (5 minutes leeway) and not older than
//     the authorization.
// Anything else: std::nullopt and *why.
struct TrustedList {
    Authorization authorization;
    SellerList list;
};
std::optional<TrustedList> checkBundle(const QJsonObject &bundle, const MasterInfo &master, qint64 lastSequence,
                                       const QDateTime &now, QString *why);

// The server's own document for the app's reports: GET /v1/keys returns
// {"authorization": ..., "revocations": [...]} - the same checks 1-2 give the
// box key to seal reports to.
std::optional<Authorization> checkKeys(const QJsonObject &keys, const MasterInfo &master, const QDateTime &now,
                                       QString *why);

} // namespace app::sellerlist
