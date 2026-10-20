#include "ui/SellerController.h"

#include "core/Settings.h"
#include "ui/Format.h"

#include <QLocale>
#include <QRegularExpression>
#include <QVariantMap>

namespace app::ui {

namespace {

const char *const kPeriods[] = {"last month", "last 6 months", "last 12 months"};

} // namespace

SellerController::SellerController(WorkFolder folder, const QString &connectionName, std::function<bool()> lookupRunning,
                                   QObject *parent)
    : QObject(parent), m_folder(std::move(folder)), m_store(connectionName), m_lookupRunning(std::move(lookupRunning)),
      m_session(new SellerSession(m_folder, connectionName, this))
{
    connect(m_session, &SellerSession::loaded, this, [this](const QString &sellerId, const QString &kind) {
        if (sellerId.compare(m_sellerId, Qt::CaseInsensitive) != 0)
            return;
        QString error;
        m_feedback = m_store.feedback(sellerId, kind, &error);
        if (!m_feedback)
            setError(error.isEmpty() ? QStringLiteral("The reviews were read but cannot be shown.") : error);
        emit changed();
    });
    refreshMarks();
}

QString SellerController::ratingText() const
{
    if (!m_percent)
        return QStringLiteral("-");
    QString text = QStringLiteral("%1% positive").arg(QLocale::c().toString(*m_percent, 'f', 1).remove(QRegularExpression(QStringLiteral("\\.0$"))));
    if (m_count)
        text += QStringLiteral(", %1").arg(countText(*m_count, QStringLiteral("rating"), QStringLiteral("ratings")));
    return text;
}

QString SellerController::ratingNote() const
{
    const Settings settings(m_folder.settingsPath());
    return lookup::ratingNote(m_percent, m_count, settings.sellerRatingThreshold(), settings.sellerFewRatings()).text;
}

bool SellerController::ratingFlagged() const
{
    const Settings settings(m_folder.settingsPath());
    return lookup::ratingNote(m_percent, m_count, settings.sellerRatingThreshold(), settings.sellerFewRatings()).flagged;
}

QString SellerController::markReasonLabel() const
{
    return m_mark ? lookup::markReasonLabel(m_mark->reason) : QString();
}

QVariantList SellerController::reasons() const
{
    QVariantList list;
    for (const lookup::MarkReason &reason : lookup::kMarkReasons)
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(reason.id)}, {QStringLiteral("label"), QString::fromLatin1(reason.label)}});
    return list;
}

QVariantList SellerController::reviews() const
{
    QVariantList list;
    if (!m_feedback)
        return list;
    for (const lookup::FeedbackReview &review : m_feedback->reviews) {
        list.append(QVariantMap{{QStringLiteral("when"), review.when},
                                {QStringLiteral("comment"), review.comment},
                                {QStringLiteral("item"), review.itemTitle},
                                {QStringLiteral("price"), review.priceText},
                                {QStringLiteral("verified"), review.verifiedPurchase}});
    }
    return list;
}

QString SellerController::reviewSummary() const
{
    if (!m_feedback)
        return {};
    const int(&counts)[3] = m_feedback->kind == QLatin1String("negative") ? m_feedback->negative : m_feedback->neutral;
    QStringList parts;
    for (int i = 0; i < 3; ++i)
        parts.append(QStringLiteral("%1: %2").arg(QString::fromLatin1(kPeriods[i]), counts[i] >= 0 ? QString::number(counts[i]) : QStringLiteral("-")));
    QString text = QStringLiteral("%1 ratings - %2").arg(m_feedback->kind == QLatin1String("negative") ? QStringLiteral("Negative")
                                                                                                        : QStringLiteral("Neutral"),
                                                        parts.join(QStringLiteral(", ")));
    if (counts[2] > m_feedback->reviews.size())
        text += QStringLiteral(" (eBay lists %1 of them on its page)").arg(m_feedback->reviews.size());
    if (m_feedback->score)
        text = QStringLiteral("Feedback score %1. ").arg(QLocale(QLocale::English, QLocale::UnitedStates).toString(*m_feedback->score)) + text;
    return text;
}

QString SellerController::readText() const
{
    if (!m_feedback)
        return {};
    return QStringLiteral("Read from eBay %1").arg(whenText(m_feedback->fetchedAt.toLocalTime(), QDateTime::currentDateTime()));
}

void SellerController::open(const QString &sellerId, const QString &displayName, double percent, int count)
{
    m_sellerId = sellerId;
    m_displayName = displayName.isEmpty() ? sellerId : displayName;
    m_percent = percent >= 0 ? std::optional<double>(percent) : std::nullopt;
    m_count = count >= 0 ? std::optional<int>(count) : std::nullopt;
    m_feedback.reset();
    m_error.clear();
    // A message of the last read belongs to the seller it was about.
    if (!m_session->busy())
        m_session->clearError();
    loadMark();
    emit changed();
}

bool SellerController::saveMark(const QString &reason, const QString &note)
{
    return saveMarkFor(m_sellerId, reason, note);
}

bool SellerController::saveMarkFor(const QString &sellerId, const QString &reason, const QString &note)
{
    QString error;
    const lookup::SellerMark mark{sellerId, reason, note.trimmed(), QDate::currentDate()};
    if (!m_store.mark(mark, &error)) {
        setError(QStringLiteral("Not saved: %1").arg(error));
        return false;
    }
    m_error.clear();
    loadMark();
    refreshMarks();
    emit changed();
    return true;
}

bool SellerController::unmark(const QString &sellerId)
{
    QString error;
    if (!m_store.unmark(sellerId, &error)) {
        setError(QStringLiteral("Not removed: %1").arg(error));
        return false;
    }
    m_error.clear();
    loadMark();
    refreshMarks();
    emit changed();
    return true;
}

void SellerController::showReviews(const QString &kind, bool refresh)
{
    m_error.clear();
    QString error;
    const int days = Settings(m_folder.settingsPath()).sellerFeedbackCacheDays();
    if (!refresh) {
        std::optional<lookup::SellerFeedback> stored = m_store.feedback(m_sellerId, kind, &error);
        if (stored && days > 0 && stored->fetchedAt.isValid() && stored->fetchedAt.addDays(days) > QDateTime::currentDateTimeUtc()) {
            m_feedback = std::move(stored);
            // Shown from the database: no page used now, no message of an
            // earlier read.
            if (!m_session->busy())
                m_session->clearError();
            emit changed();
            return;
        }
    }
    if (m_lookupRunning && m_lookupRunning()) {
        setError(QStringLiteral("A lookup is using the browser; open the reviews when it has finished."));
        return;
    }
    m_session->read(m_sellerId, kind);
    emit changed();
}

void SellerController::refreshMarks()
{
    m_marks.clear();
    QString error;
    const std::optional<QList<lookup::SellerMark>> marks = m_store.marks(&error);
    if (!marks) {
        setError(QStringLiteral("The marked sellers cannot be read: %1").arg(error));
    } else {
        for (const lookup::SellerMark &mark : *marks) {
            m_marks.append(QVariantMap{{QStringLiteral("sellerId"), mark.sellerId},
                                       {QStringLiteral("reason"), mark.reason},
                                       {QStringLiteral("reasonLabel"), lookup::markReasonLabel(mark.reason)},
                                       {QStringLiteral("note"), mark.note},
                                       {QStringLiteral("date"), mark.markedOn.toString(Qt::ISODate)}});
        }
    }
    emit marksChanged();
}

void SellerController::loadMark()
{
    QString error;
    m_mark = m_store.markOf(m_sellerId, &error);
    if (!error.isEmpty())
        setError(error);
}

void SellerController::setError(const QString &error)
{
    m_error = error;
    emit changed();
}

} // namespace app::ui
