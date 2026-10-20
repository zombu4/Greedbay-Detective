#include "lookup/Sellers.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace app::lookup {

namespace {

QJsonArray countsJson(const int (&counts)[3])
{
    return QJsonArray{counts[0], counts[1], counts[2]};
}

bool countsFromJson(const QJsonValue &value, int (&counts)[3])
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        if (!array.at(i).isDouble())
            return false;
        counts[i] = array.at(i).toInt(-1);
    }
    return true;
}

} // namespace

bool isMarkReason(const QString &id)
{
    return std::any_of(std::begin(kMarkReasons), std::end(kMarkReasons),
                       [&id](const MarkReason &reason) { return id == QLatin1String(reason.id); });
}

QString markReasonLabel(const QString &id)
{
    for (const MarkReason &reason : kMarkReasons) {
        if (id == QLatin1String(reason.id))
            return QString::fromLatin1(reason.label);
    }
    return id;
}

bool isSellerId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._*-]{1,64}$"));
    return pattern.match(id).hasMatch();
}

RatingNote ratingNote(std::optional<double> percent, std::optional<int> count, double threshold, int fewRatings)
{
    RatingNote note;
    if (!percent)
        return note;
    const QString pct = QString::number(*percent, 'f', *percent == std::floor(*percent) ? 0 : 1);
    const QString limit = QString::number(threshold, 'f', threshold == std::floor(threshold) ? 0 : 1);
    if (count && *count < fewRatings) {
        note.text = QStringLiteral("Seller rating %1% - only %2 %3, too few to judge").arg(pct).arg(*count)
                        .arg(*count == 1 ? QStringLiteral("rating") : QStringLiteral("ratings"));
        note.badge = QStringLiteral("only %1 %2").arg(*count).arg(*count == 1 ? QStringLiteral("rating") : QStringLiteral("ratings"));
        return note;
    }
    if (*percent < threshold) {
        note.flagged = true;
        note.text = QStringLiteral("Seller rating %1% (below %2%) - more problems than usual").arg(pct, limit)
                    + (count ? QStringLiteral(" (%1 ratings)").arg(*count) : QString());
        note.badge = QStringLiteral("rating %1%").arg(pct);
    }
    return note;
}

QJsonObject SellerFeedback::toJson() const
{
    QJsonArray reviewArray;
    for (const FeedbackReview &review : reviews) {
        reviewArray.append(QJsonObject{{QStringLiteral("when"), review.when},
                                       {QStringLiteral("comment"), review.comment},
                                       {QStringLiteral("item"), review.itemTitle},
                                       {QStringLiteral("price"), review.priceText},
                                       {QStringLiteral("verified"), review.verifiedPurchase}});
    }
    QJsonObject object{{QStringLiteral("sellerId"), sellerId},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("positive"), countsJson(positive)},
                       {QStringLiteral("neutral"), countsJson(neutral)},
                       {QStringLiteral("negative"), countsJson(negative)},
                       {QStringLiteral("reviews"), reviewArray}};
    if (score)
        object.insert(QStringLiteral("score"), *score);
    return object;
}

std::optional<SellerFeedback> SellerFeedback::fromJson(const QJsonObject &data)
{
    SellerFeedback feedback;
    feedback.sellerId = data.value(QStringLiteral("sellerId")).toString();
    feedback.kind = data.value(QStringLiteral("kind")).toString();
    if (!isSellerId(feedback.sellerId)
        || (feedback.kind != QLatin1String("negative") && feedback.kind != QLatin1String("neutral"))
        || !countsFromJson(data.value(QStringLiteral("positive")), feedback.positive)
        || !countsFromJson(data.value(QStringLiteral("neutral")), feedback.neutral)
        || !countsFromJson(data.value(QStringLiteral("negative")), feedback.negative)
        || !data.value(QStringLiteral("reviews")).isArray())
        return std::nullopt;
    if (data.value(QStringLiteral("score")).isDouble())
        feedback.score = data.value(QStringLiteral("score")).toInt();
    for (const QJsonValue &value : data.value(QStringLiteral("reviews")).toArray()) {
        const QJsonObject object = value.toObject();
        FeedbackReview review;
        review.when = object.value(QStringLiteral("when")).toString();
        review.comment = object.value(QStringLiteral("comment")).toString();
        review.itemTitle = object.value(QStringLiteral("item")).toString();
        review.priceText = object.value(QStringLiteral("price")).toString();
        review.verifiedPurchase = object.value(QStringLiteral("verified")).toBool();
        if (!review.comment.isEmpty() || !review.when.isEmpty())
            feedback.reviews.append(review);
    }
    return feedback;
}

QString feedbackUrl(const QString &sellerId, const QString &kind)
{
    // The classic view's address as its own "Switch back to classic view"
    // link gives it (recon 2026-10-20).
    return QStringLiteral("https://www.ebay.com/fdbk/feedback_profile/%1?filter=feedback_page%3ARECEIVED_AS_SELLER&sort=RELEVANCEV2#%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(sellerId)), kind);
}

} // namespace app::lookup
