#pragma once

#include "lookup/Sellers.h"

#include <QString>

#include <optional>

namespace app::lookup {

// The user's marked sellers and the cached seller reviews, in the work
// folder's database (the open connection `connectionName`, see db/Database;
// tables marked_sellers and seller_feedback, migration 0005). Local only:
// nothing here is uploaded or shared.
class SellerStore {
public:
    explicit SellerStore(QString connectionName);

    // Adds or replaces the mark of mark.sellerId. Refused (false, *error)
    // for a seller id that is no user id, an unknown reason or a note over
    // 2000 characters. A replaced mark keeps its first date.
    bool mark(const SellerMark &mark, QString *error) const;
    // Removes the mark; no mark is no error.
    bool unmark(const QString &sellerId, QString *error) const;
    // The mark of one seller (case-insensitive), std::nullopt when none or
    // on a read error (*error set then).
    std::optional<SellerMark> markOf(const QString &sellerId, QString *error) const;
    // Every mark, the newest first; std::nullopt and *error on a read error.
    std::optional<QList<SellerMark>> marks(QString *error) const;

    // Stores one read of a seller's feedback page (its kind), replacing the
    // last one of that kind.
    bool saveFeedback(const SellerFeedback &feedback, QString *error) const;
    // The last read of that kind, std::nullopt when none (or unreadable).
    std::optional<SellerFeedback> feedback(const QString &sellerId, const QString &kind, QString *error) const;

private:
    QString m_connectionName;
};

} // namespace app::lookup
