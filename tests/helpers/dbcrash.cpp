// Test helper for tst_database: creates a database with the synthetic
// migrations 1-3 and exits at once, without any cleanup, right after
// migration 2 has committed, like a process killed between migrations.
//
// usage: dbcrash <database file> <backups folder>
#include "db/Database.h"
#include "support/TestMigrations.h"

#include <QCoreApplication>

#include <cstdlib>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (argc != 3)
        return 2;
    app::Database::Options options;
    options.afterMigration = [](int version) {
        if (version == 2)
            std::_Exit(3);
    };
    const app::Database::Result result =
        app::Database::open(QStringLiteral("crash"), QString::fromLocal8Bit(argv[1]),
                            QString::fromLocal8Bit(argv[2]), QStringLiteral("AppName"),
                            parentChildMigrations(3), options);
    return result.ok() ? 0 : 1;
}
