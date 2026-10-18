# appcore: work folder, settings, logging, single-instance lock, version and
# the database layer. The application executable is added together with the
# screens; until then appcore is built and tested on its own.
qt_add_library(appcore STATIC
    core/Logging.cpp
    core/Logging.h
    core/PathChecks.h
    core/Settings.cpp
    core/Settings.h
    core/SingleInstance.cpp
    core/SingleInstance.h
    core/WorkFolder.cpp
    core/WorkFolder.h
    db/Database.cpp
    db/Database.h
    db/Migrations.cpp
    db/Migrations.h
    db/SqlScript.cpp
    db/SqlScript.h
)

configure_file(core/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/core/Version.h" @ONLY)

target_include_directories(appcore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(appcore PUBLIC Qt6::Core Qt6::Sql)

app_enable_warnings(appcore)

# The schema migrations ship inside the binary as :/migrations/NNNN_name.sql.
qt_add_resources(appcore "migrations"
    PREFIX "/migrations"
    BASE "db/migrations"
    FILES
        db/migrations/0001_init.sql
)
