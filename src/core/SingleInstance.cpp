#include "core/SingleInstance.h"

namespace app {

SingleInstance::SingleInstance(const QString &lockPath) : m_lock(lockPath)
{
    // The app holds the lock for hours. With the default stale time (30 s) a
    // lock older than that counts as stale even while its owner runs, and only
    // the operating system's file lock would then stop a second instance. 0
    // turns the age check off; QLockFile still treats a lock as stale when the
    // process that took it is no longer running (Qt 6.12 qlockfile.cpp).
    m_lock.setStaleLockTime(0);
}

SingleInstance::~SingleInstance()
{
    release();
}

bool SingleInstance::tryAcquire()
{
    m_error.clear();
    if (m_lock.isLocked())
        return true;
    if (m_lock.tryLock(0))
        return true;
    switch (m_lock.error()) {
    case QLockFile::LockFailedError:
        m_error = QStringLiteral("another instance is using this work folder");
        break;
    case QLockFile::PermissionError:
        m_error = QStringLiteral("no permission to create the lock file");
        break;
    case QLockFile::NoError:
    case QLockFile::UnknownError:
        m_error = QStringLiteral("the lock file could not be created");
        break;
    }
    return false;
}

void SingleInstance::release()
{
    if (m_lock.isLocked())
        m_lock.unlock();
}

} // namespace app
