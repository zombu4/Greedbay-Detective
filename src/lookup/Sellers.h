#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace app::lookup {

// Sellers (owner decisions 2026-10-20): the user's own marked sellers, the
// low-rating flag and the seller's negative and neutral reviews.
//
// A seller is known by its eBay user id, the "username" of the feedback
// links on the item page; search cards show the same id. Ids are compared
// without regard to case.

// Why the user marked a seller. The ids are stored; the labels are shown.
struct MarkReason {
    const char *id;
    const char *label;
};
inline constexpr MarkReason kMarkReasons[] = {
    {"junk", "got junk"},
    {"not_as_described", "not as described"},
    {"scam", "scam"},
    {"never_shipped", "never shipped"},
    {"fake_best_offer", "fake Best Offer (auto-declines)"},
    {"overpriced_junk", "overpriced junk"},
    {"other", "other"},
};
bool isMarkReason(const QString &id);
QString markReasonLabel(const QString &id);

struct SellerMark {
    QString sellerId;
    QString reason; // a kMarkReasons id
    QString note;
    QDate markedOn;
};

// The settings the rating flag uses (Settings "sellers/...").
struct SellerRules {
    double threshold = 96.0;
    int fewRatings = 20;
};

// A user id as eBay writes it: letters, digits and . _ - * (1-64 chars).
bool isSellerId(const QString &id);

// The low-rating flag (owner: "flag sellers with a review rating below 96%").
// Information only: no number changes.
struct RatingNote {
    bool flagged = false; // below the threshold, with enough ratings
    QString text;         // the sentence for the flag card or the panel; empty when nothing to say
    QString badge;        // short text for a row badge; empty when no badge
};
// percent / count: what the page showed (std::nullopt: not shown, stays "-").
// threshold: the setting (default 96.0); fewRatings: below this many ratings
// the app says "only N ratings" instead of judging (default 20).
RatingNote ratingNote(std::optional<double> percent, std::optional<int> count, double threshold, int fewRatings);

// One review from the classic feedback page.
struct FeedbackReview {
    QString when;       // "Past month", "Past 6 months", "More than a year ago"
    QString comment;
    QString itemTitle;  // may be empty
    QString priceText;  // "US $255.00", may be empty
    bool verifiedPurchase = false;
};

// What one read of a seller's feedback page gave: the rating counts of the
// page's table (1 month, 6 months, 12 months) and the reviews of one kind
// ("negative" or "neutral") of the last 12 months, as the page lists them
// (at most one page, 25).
struct SellerFeedback {
    QString sellerId;
    QString kind;
    std::optional<int> score;
    int positive[3] = {-1, -1, -1}; // -1: not shown
    int neutral[3] = {-1, -1, -1};
    int negative[3] = {-1, -1, -1};
    QList<FeedbackReview> reviews;
    QDateTime fetchedAt; // UTC

    QJsonObject toJson() const;
    // std::nullopt when `data` is not the feedback extractor's data object.
    static std::optional<SellerFeedback> fromJson(const QJsonObject &data);
};

// The classic feedback page of a seller, with the kind to list in the URL's
// fragment (never sent to eBay; the extractor reads it): kind is "negative"
// or "neutral".
QString feedbackUrl(const QString &sellerId, const QString &kind);

} // namespace app::lookup
