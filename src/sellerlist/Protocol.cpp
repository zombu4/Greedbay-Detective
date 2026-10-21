#include "sellerlist/Protocol.h"

#include "lookup/Sellers.h"

#include <sodium.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace app::sellerlist {

namespace {

QString timeText(const QDateTime &time) { return time.toUTC().toString(Qt::ISODate); }

std::optional<QDateTime> timeOf(const QJsonValue &value)
{
    const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODate);
    if (!time.isValid())
        return std::nullopt;
    return time.toUTC();
}

std::optional<qint64> wholeOf(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (number < 0 || number > 9007199254740991.0 || number != static_cast<double>(static_cast<qint64>(number)))
        return std::nullopt;
    return static_cast<qint64>(number);
}

bool isMonth(const QString &month)
{
    static const QRegularExpression pattern(QStringLiteral("^20\\d\\d-(0[1-9]|1[0-2])$"));
    return pattern.match(month).hasMatch();
}

bool isItemId(const QString &itemId)
{
    static const QRegularExpression pattern(QStringLiteral("^\\d{9,15}$"));
    return pattern.match(itemId).hasMatch();
}

bool isHexId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{16,64}$"));
    return pattern.match(id).hasMatch();
}

QByteArray compact(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

} // namespace

std::optional<MasterInfo> MasterInfo::fromJson(const QJsonObject &object)
{
    const std::optional<SignPublic> master = SignPublic::fromJson(object.value(QStringLiteral("master")).toObject());
    const QString onion = object.value(QStringLiteral("onion")).toString();
    if (!master || !isOnionAddress(onion))
        return std::nullopt;
    return MasterInfo{*master, onion};
}

bool isOnionAddress(const QString &host)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z2-7]{56}\\.onion$"));
    return pattern.match(host).hasMatch();
}

std::optional<QJsonObject> signDocument(const QJsonObject &body, const SignKeys &keys)
{
    const std::optional<HybridSignature> signature = sign(compact(body), keys);
    if (!signature)
        return std::nullopt;
    return QJsonObject{{QStringLiteral("body"), body}, {QStringLiteral("sig"), signature->toJson()}};
}

std::optional<QJsonObject> verifiedBody(const QJsonObject &document, const SignPublic &key)
{
    if (!document.value(QStringLiteral("body")).isObject() || !document.value(QStringLiteral("sig")).isObject())
        return std::nullopt;
    const QJsonObject body = document.value(QStringLiteral("body")).toObject();
    const std::optional<HybridSignature> signature = HybridSignature::fromJson(document.value(QStringLiteral("sig")).toObject());
    if (!signature || !verify(compact(body), *signature, key))
        return std::nullopt;
    return body;
}

QJsonObject Authorization::toBody() const
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("authorization")},
                       {QStringLiteral("serverSign"), serverSign.toJson()},
                       {QStringLiteral("serverBox"), serverBox.toJson()},
                       {QStringLiteral("onion"), onion},
                       {QStringLiteral("sequence"), static_cast<double>(sequence)},
                       {QStringLiteral("validFrom"), timeText(validFrom)},
                       {QStringLiteral("validUntil"), timeText(validUntil)}};
}

std::optional<Authorization> Authorization::fromBody(const QJsonObject &body)
{
    if (body.value(QStringLiteral("type")).toString() != QLatin1String("authorization"))
        return std::nullopt;
    const std::optional<SignPublic> sign = SignPublic::fromJson(body.value(QStringLiteral("serverSign")).toObject());
    const std::optional<BoxPublic> box = BoxPublic::fromJson(body.value(QStringLiteral("serverBox")).toObject());
    const std::optional<qint64> sequence = wholeOf(body.value(QStringLiteral("sequence")));
    const std::optional<QDateTime> from = timeOf(body.value(QStringLiteral("validFrom")));
    const std::optional<QDateTime> until = timeOf(body.value(QStringLiteral("validUntil")));
    const QString onion = body.value(QStringLiteral("onion")).toString();
    if (!sign || !box || !sequence || !from || !until || *until <= *from || !isOnionAddress(onion))
        return std::nullopt;
    return Authorization{*sign, *box, onion, *sequence, *from, *until};
}

QJsonObject Revocation::toBody() const
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("revocation")},
                       {QStringLiteral("serverSign"), serverSign.toJson()},
                       {QStringLiteral("sequence"), static_cast<double>(sequence)},
                       {QStringLiteral("revokedAt"), timeText(revokedAt)}};
}

std::optional<Revocation> Revocation::fromBody(const QJsonObject &body)
{
    if (body.value(QStringLiteral("type")).toString() != QLatin1String("revocation"))
        return std::nullopt;
    const std::optional<SignPublic> sign = SignPublic::fromJson(body.value(QStringLiteral("serverSign")).toObject());
    const std::optional<qint64> sequence = wholeOf(body.value(QStringLiteral("sequence")));
    const std::optional<QDateTime> at = timeOf(body.value(QStringLiteral("revokedAt")));
    if (!sign || !sequence || !at)
        return std::nullopt;
    return Revocation{*sign, *sequence, *at};
}

QJsonObject PublishedReport::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id},     {QStringLiteral("reason"), reason},
                       {QStringLiteral("text"), text}, {QStringLiteral("month"), month},
                       {QStringLiteral("itemChecked"), itemChecked}};
}

std::optional<PublishedReport> PublishedReport::fromJson(const QJsonObject &object)
{
    PublishedReport report{object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("reason")).toString(),
                           object.value(QStringLiteral("text")).toString(), object.value(QStringLiteral("month")).toString(),
                           object.value(QStringLiteral("itemChecked")).toBool()};
    if (!isHexId(report.id) || !lookup::isMarkReason(report.reason) || !isCleanText(report.text, kMaxReportText)
        || !isMonth(report.month))
        return std::nullopt;
    return report;
}

QJsonObject SellerList::toBody() const
{
    QJsonArray sellerArray;
    for (const ListedSeller &seller : sellers) {
        QJsonArray reports;
        for (const PublishedReport &report : seller.reports)
            reports.append(report.toJson());
        sellerArray.append(QJsonObject{{QStringLiteral("sellerId"), seller.sellerId}, {QStringLiteral("reports"), reports}});
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("sellerlist")},
                       {QStringLiteral("sequence"), static_cast<double>(sequence)},
                       {QStringLiteral("signedAt"), timeText(signedAt)},
                       {QStringLiteral("authorizationSequence"), static_cast<double>(authorizationSequence)},
                       {QStringLiteral("sellers"), sellerArray}};
}

std::optional<SellerList> SellerList::fromBody(const QJsonObject &body)
{
    if (body.value(QStringLiteral("type")).toString() != QLatin1String("sellerlist"))
        return std::nullopt;
    const std::optional<qint64> sequence = wholeOf(body.value(QStringLiteral("sequence")));
    const std::optional<qint64> authorization = wholeOf(body.value(QStringLiteral("authorizationSequence")));
    const std::optional<QDateTime> generated = timeOf(body.value(QStringLiteral("signedAt")));
    if (!sequence || !authorization || !generated || !body.value(QStringLiteral("sellers")).isArray())
        return std::nullopt;
    SellerList list;
    list.sequence = *sequence;
    list.authorizationSequence = *authorization;
    list.signedAt = *generated;
    for (const QJsonValue &value : body.value(QStringLiteral("sellers")).toArray()) {
        const QJsonObject object = value.toObject();
        ListedSeller seller;
        seller.sellerId = object.value(QStringLiteral("sellerId")).toString();
        if (!lookup::isSellerId(seller.sellerId) || !object.value(QStringLiteral("reports")).isArray())
            return std::nullopt;
        for (const QJsonValue &reportValue : object.value(QStringLiteral("reports")).toArray()) {
            const std::optional<PublishedReport> report = PublishedReport::fromJson(reportValue.toObject());
            if (!report)
                return std::nullopt;
            seller.reports.append(*report);
        }
        list.sellers.append(seller);
    }
    return list;
}

const ListedSeller *SellerList::find(const QString &sellerId) const
{
    for (const ListedSeller &seller : sellers) {
        if (seller.sellerId.compare(sellerId, Qt::CaseInsensitive) == 0)
            return &seller;
    }
    return nullptr;
}

QJsonObject Report::toJson() const
{
    QJsonObject object{{QStringLiteral("type"), QStringLiteral("report")},
                       {QStringLiteral("sellerId"), sellerId},
                       {QStringLiteral("reason"), reason},
                       {QStringLiteral("text"), text},
                       {QStringLiteral("itemId"), itemId},
                       {QStringLiteral("month"), month},
                       {QStringLiteral("createdAt"), timeText(createdAt)},
                       {QStringLiteral("installKey"), QString::fromLatin1(installKey.toBase64())}};
    if (!installSignature.isEmpty())
        object.insert(QStringLiteral("installSignature"), QString::fromLatin1(installSignature.toBase64()));
    return object;
}

std::optional<Report> Report::fromJson(const QJsonObject &object)
{
    if (object.value(QStringLiteral("type")).toString() != QLatin1String("report"))
        return std::nullopt;
    const std::optional<QDateTime> created = timeOf(object.value(QStringLiteral("createdAt")));
    const QByteArray::FromBase64Result key = QByteArray::fromBase64Encoding(
        object.value(QStringLiteral("installKey")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const QByteArray::FromBase64Result signature = QByteArray::fromBase64Encoding(
        object.value(QStringLiteral("installSignature")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!created || !key || !signature)
        return std::nullopt;
    return Report{object.value(QStringLiteral("sellerId")).toString(),
                  object.value(QStringLiteral("reason")).toString(),
                  object.value(QStringLiteral("text")).toString(),
                  object.value(QStringLiteral("itemId")).toString(),
                  object.value(QStringLiteral("month")).toString(),
                  *created,
                  key.decoded,
                  signature.decoded};
}

QByteArray Report::signedBytes() const
{
    Report unsigned_ = *this;
    unsigned_.installSignature.clear();
    return compact(unsigned_.toJson());
}

QString Report::problem() const
{
    if (!lookup::isSellerId(sellerId))
        return QStringLiteral("the seller id is not one");
    if (!lookup::isMarkReason(reason))
        return QStringLiteral("the reason is not one the app knows");
    if (!isCleanText(text, kMaxReportText))
        return QStringLiteral("the text is empty, too long or has control characters");
    if (!isItemId(itemId))
        return QStringLiteral("the item number is not one");
    if (!isMonth(month))
        return QStringLiteral("the month is not one");
    if (installKey.size() != crypto_sign_ed25519_PUBLICKEYBYTES || installSignature.size() != crypto_sign_ed25519_BYTES)
        return QStringLiteral("the installation key or its signature is missing");
    const QByteArray message = signedBytes();
    if (crypto_sign_ed25519_verify_detached(reinterpret_cast<const unsigned char *>(installSignature.constData()),
                                            reinterpret_cast<const unsigned char *>(message.constData()),
                                            static_cast<unsigned long long>(message.size()),
                                            reinterpret_cast<const unsigned char *>(installKey.constData()))
        != 0)
        return QStringLiteral("the installation signature does not verify");
    return {};
}

QJsonObject Dispute::toJson() const
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("dispute")},
                       {QStringLiteral("sellerId"), sellerId},
                       {QStringLiteral("reportId"), reportId},
                       {QStringLiteral("text"), text}};
}

std::optional<Dispute> Dispute::fromJson(const QJsonObject &object)
{
    if (object.value(QStringLiteral("type")).toString() != QLatin1String("dispute"))
        return std::nullopt;
    return Dispute{object.value(QStringLiteral("sellerId")).toString(), object.value(QStringLiteral("reportId")).toString(),
                   object.value(QStringLiteral("text")).toString()};
}

QString Dispute::problem() const
{
    if (!lookup::isSellerId(sellerId))
        return QStringLiteral("the seller id is not one");
    if (!reportId.isEmpty() && !isHexId(reportId))
        return QStringLiteral("the report id is not one");
    if (!isCleanText(text, kMaxDisputeText))
        return QStringLiteral("the text is empty, too long or has control characters");
    return {};
}

bool isCleanText(const QString &text, int maxLength)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > maxLength)
        return false;
    for (const QChar c : trimmed) {
        if ((c.category() == QChar::Other_Control && c != QLatin1Char('\n')) || c.category() == QChar::Other_Format)
            return false;
    }
    return true;
}

} // namespace app::sellerlist
