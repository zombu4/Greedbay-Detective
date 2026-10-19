#include "core/PageBudget.h"

#include "core/PathChecks.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

namespace app {

namespace {

// A count file is a few dozen bytes; anything much larger is not one.
constexpr qint64 kMaxFileBytes = 4096;

} // namespace

PageBudget::PageBudget(QString filePath, int dailyCap, Clock today)
    : m_path(std::move(filePath)), m_cap(dailyCap), m_today(std::move(today))
{
}

bool PageBudget::load(State *state, QString *error) const
{
    const QDate today = m_today();
    *state = State{today, 0};
    if (isLink(m_path)) {
        if (error)
            *error = QStringLiteral("the page counter file %1 is a link; links are never followed").arg(m_path);
        return false;
    }
    if (!somethingAt(m_path))
        return true;
    QFile file(m_path);
    if (!isPlainFile(m_path) || QFileInfo(m_path).size() > kMaxFileBytes || !file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("the page counter file %1 cannot be read; delete it to start counting again")
                         .arg(m_path);
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject object = document.object();
    const QDate date = QDate::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
    const QJsonValue pages = object.value(QStringLiteral("pages"));
    const double count = pages.toDouble(-1);
    if (!document.isObject() || !date.isValid() || !pages.isDouble() || count < 0 || count > 1e9
        || count != static_cast<double>(static_cast<int>(count))) {
        if (error)
            *error = QStringLiteral("the page counter file %1 is damaged; delete it to start counting again").arg(m_path);
        return false;
    }
    if (date >= today)
        *state = State{date, static_cast<int>(count)};
    return true;
}

int PageBudget::usedToday(QString *error) const
{
    State state;
    return load(&state, error) ? state.pages : -1;
}

PageBudget::Take PageBudget::take(QString *error)
{
    State state;
    if (!load(&state, error))
        return Take::FileError;
    if (state.pages >= m_cap) {
        if (error)
            *error = QStringLiteral("Today's page limit is reached: the app has loaded %1 eBay pages today (limit %2, "
                                    "in Settings). It loads more pages tomorrow.")
                         .arg(state.pages)
                         .arg(m_cap);
        return Take::CapReached;
    }
    const QJsonObject object{{QStringLiteral("date"), state.date.toString(Qt::ISODate)},
                             {QStringLiteral("pages"), state.pages + 1}};
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("the page counter file %1 cannot be saved: %2").arg(m_path, file.errorString());
        return Take::FileError;
    }
    return Take::Taken;
}

} // namespace app
