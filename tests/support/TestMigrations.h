#pragma once

// Synthetic migrations shared by tst_database and the dbcrash helper.
//   1  parent and child tables, child references parent with ON DELETE CASCADE
//   2  rebuilds parent with an extra column (create new, copy, drop old,
//      rename), the way SQLite's 12-step procedure changes a table; with
//      foreign keys on, dropping parent would delete every child row
//   3  adds a table
#include "db/Migrations.h"

inline QList<app::Migration> parentChildMigrations(int upToVersion)
{
    const QList<app::Migration> all = {
        app::Migration{1,
                       QStringLiteral("parent_child"),
                       {QStringLiteral("CREATE TABLE parent (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"),
                        QStringLiteral("CREATE TABLE child (id INTEGER PRIMARY KEY, parent_id INTEGER NOT NULL "
                                       "REFERENCES parent(id) ON DELETE CASCADE)")}},
        app::Migration{2,
                       QStringLiteral("rebuild_parent"),
                       {QStringLiteral("CREATE TABLE new_parent (id INTEGER PRIMARY KEY, name TEXT NOT NULL, note TEXT)"),
                        QStringLiteral("INSERT INTO new_parent (id, name) SELECT id, name FROM parent"),
                        QStringLiteral("DROP TABLE parent"),
                        QStringLiteral("ALTER TABLE new_parent RENAME TO parent")}},
        app::Migration{3, QStringLiteral("add_extra"), {QStringLiteral("CREATE TABLE extra (id INTEGER PRIMARY KEY)")}},
    };
    return all.mid(0, upToVersion);
}
