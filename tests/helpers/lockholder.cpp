// Test helper for tst_singleinstance: takes the lock in a separate process.
//
// usage: lockholder <lock file> hold|crash
//   hold   take the lock, print "locked", wait for a line on stdin, release
//   crash  take the lock, print "locked", exit at once without releasing it
#include "core/SingleInstance.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (argc != 3)
        return 2;
    app::SingleInstance lock(QString::fromLocal8Bit(argv[1]));
    if (!lock.tryAcquire()) {
        std::fputs("failed\n", stdout);
        return 1;
    }
    std::fputs("locked\n", stdout);
    std::fflush(stdout);
    if (std::strcmp(argv[2], "crash") == 0)
        std::_Exit(0);
    char line[16];
    if (!std::fgets(line, sizeof line, stdin))
        return 0;
    return 0;
}
