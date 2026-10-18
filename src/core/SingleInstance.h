#pragma once

#include <QLockFile>
#include <QString>

namespace app {

// Makes sure only one instance of the app uses a work folder at a time, through
// a lock file (<work>/.lock). Other programs can test the same lock to see
// whether the app is running.
class SingleInstance {
public:
    explicit SingleInstance(const QString &lockPath);
    ~SingleInstance();

    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;

    // Takes the lock without waiting. Returns true when this process now holds
    // it, false when another process holds it or the lock file cannot be
    // created; error() then says which.
    bool tryAcquire();

    // Gives the lock up. Safe to call when it is not held.
    void release();

    bool isHeld() const { return m_lock.isLocked(); }
    QString error() const { return m_error; }

private:
    QLockFile m_lock;
    QString m_error;
};

} // namespace app
