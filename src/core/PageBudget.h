#pragma once

#include <QDate>
#include <QString>

#include <functional>

namespace app {

// The daily page cap (owner decision: 300 eBay page loads per day, a
// setting): counts every page load the app makes and refuses one more when
// today's count has reached the cap.
//
// The count lives in the work folder (WorkFolder::pageCountPath()) as
//   {"date":"YYYY-MM-DD","pages":N}
// so it holds across app restarts. "Today" is the local calendar date. A
// stored date before today starts a new day at 0. A stored date after today
// (the clock was set back) keeps the stored count, so setting the clock back
// never gives extra pages.
//
// A file that exists but cannot be read as that object is not overwritten:
// take() refuses and says so (the user can delete the file). A count that
// cannot be saved also refuses the page: a page is never loaded uncounted.
class PageBudget {
public:
    using Clock = std::function<QDate()>;

    enum class Take {
        Taken,      // counted; the page may be loaded
        CapReached, // today's count is at the cap
        FileError,  // the count file cannot be read or saved
    };

    // `today` returns the current local date; tests pass their own.
    PageBudget(QString filePath, int dailyCap, Clock today = &QDate::currentDate);

    int dailyCap() const { return m_cap; }

    // Pages counted for today. -1 (and *error) when the file cannot be read.
    int usedToday(QString *error = nullptr) const;

    // Counts one page when today's count is below the cap. Unless Taken,
    // *error is a plain sentence for the user.
    Take take(QString *error);

private:
    struct State {
        QDate date;
        int pages = 0;
    };
    // The stored state as it applies today; false when the file is unreadable.
    bool load(State *state, QString *error) const;

    QString m_path;
    int m_cap;
    Clock m_today;
};

} // namespace app
