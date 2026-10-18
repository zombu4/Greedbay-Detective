# Unit tests (QtTest). Plain add_executable, not qt_add_executable, so no macOS
# bundle is created for a test.
function(app_add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE appcore Qt6::Test)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE TEST_APP_NAME="${APP_NAME}")
    app_enable_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Helper programs started by tests; built like tests but not run by ctest.
# Tests and helpers get the product identifier as TEST_APP_NAME (a string
# literal), so names and paths in tests follow APP_NAME.
function(app_add_test_helper name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE appcore)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE TEST_APP_NAME="${APP_NAME}")
    app_enable_warnings(${name})
endfunction()

app_add_unit_test(tst_version tst_version.cpp)
app_add_unit_test(tst_settings tst_settings.cpp support/TestFiles.h)
app_add_unit_test(tst_pathchecks tst_pathchecks.cpp support/TestFiles.h)
app_add_unit_test(tst_workfolder tst_workfolder.cpp support/TestFiles.h)
app_add_unit_test(tst_singleinstance tst_singleinstance.cpp)
app_add_unit_test(tst_logging tst_logging.cpp support/TestFiles.h)
app_add_unit_test(tst_sqlscript tst_sqlscript.cpp)
app_add_unit_test(tst_migrations tst_migrations.cpp support/TestFiles.h)
app_add_unit_test(tst_database tst_database.cpp support/TestFiles.h support/TestMigrations.h)

app_add_test_helper(lockholder helpers/lockholder.cpp)
app_add_test_helper(dbcrash helpers/dbcrash.cpp support/TestMigrations.h)

target_compile_definitions(tst_singleinstance PRIVATE LOCKHOLDER_PATH="$<TARGET_FILE:lockholder>")
add_dependencies(tst_singleinstance lockholder)

target_compile_definitions(tst_database PRIVATE DBCRASH_PATH="$<TARGET_FILE:dbcrash>")
add_dependencies(tst_database dbcrash)

target_compile_definitions(tst_version PRIVATE
    EXPECTED_DISPLAY_NAME="${APP_DISPLAY_NAME}"
    EXPECTED_PACKAGE_NAME="${APP_PACKAGE_NAME}"
    EXPECTED_BUNDLE_ID="${APP_BUNDLE_ID}"
    EXPECTED_VERSION="${PROJECT_VERSION}")
