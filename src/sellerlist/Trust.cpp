#include "sellerlist/Trust.h"

#include <QJsonArray>

namespace app::sellerlist {

namespace {

constexpr qint64 kFutureLeewaySeconds = 300;

std::optional<Authorization> checkedAuthorization(const QJsonObject &bundle, const MasterInfo &master, const QDateTime &now,
                                                  QString *why)
{
    const std::optional<QJsonObject> body = verifiedBody(bundle.value(QStringLiteral("authorization")).toObject(), master.master);
    if (!body) {
        *why = QStringLiteral("the server's key is not signed by the list owner's master key");
        return std::nullopt;
    }
    const std::optional<Authorization> authorization = Authorization::fromBody(*body);
    if (!authorization) {
        *why = QStringLiteral("the server's key document is not readable");
        return std::nullopt;
    }
    if (authorization->host != master.host) {
        *why = QStringLiteral("the server's key document names another host name");
        return std::nullopt;
    }
    if (!authorization->validAt(now)) {
        *why = QStringLiteral("the server's key is not valid at this time");
        return std::nullopt;
    }
    for (const QJsonValue &value : bundle.value(QStringLiteral("revocations")).toArray()) {
        const std::optional<QJsonObject> revocationBody = verifiedBody(value.toObject(), master.master);
        if (!revocationBody)
            continue; // not from the master: no effect either way
        const std::optional<Revocation> revocation = Revocation::fromBody(*revocationBody);
        if (revocation && revocation->serverSign == authorization->serverSign) {
            *why = QStringLiteral("the server's key was revoked by the list owner");
            return std::nullopt;
        }
    }
    return authorization;
}

} // namespace

std::optional<TrustedList> checkBundle(const QJsonObject &bundle, const MasterInfo &master, qint64 lastSequence,
                                       const QDateTime &now, QString *why)
{
    const std::optional<Authorization> authorization = checkedAuthorization(bundle, master, now, why);
    if (!authorization)
        return std::nullopt;
    const std::optional<QJsonObject> body = verifiedBody(bundle.value(QStringLiteral("list")).toObject(), authorization->serverSign);
    if (!body) {
        *why = QStringLiteral("the list is not signed by the authorized server key");
        return std::nullopt;
    }
    const std::optional<SellerList> list = SellerList::fromBody(*body);
    if (!list) {
        *why = QStringLiteral("the list is not readable");
        return std::nullopt;
    }
    if (list->authorizationSequence != authorization->sequence) {
        *why = QStringLiteral("the list names another key document");
        return std::nullopt;
    }
    if (list->sequence <= lastSequence) {
        *why = QStringLiteral("the list is not newer than the one the app has (sequence %1, have %2)")
                   .arg(list->sequence)
                   .arg(lastSequence);
        return std::nullopt;
    }
    if (list->signedAt > now.addSecs(kFutureLeewaySeconds) || list->signedAt < authorization->validFrom) {
        *why = QStringLiteral("the list's time is not plausible");
        return std::nullopt;
    }
    return TrustedList{*authorization, *list};
}

std::optional<Authorization> checkKeys(const QJsonObject &keys, const MasterInfo &master, const QDateTime &now, QString *why)
{
    return checkedAuthorization(keys, master, now, why);
}

} // namespace app::sellerlist
