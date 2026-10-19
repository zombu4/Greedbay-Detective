# Unit tests (QtTest). Plain add_executable, not qt_add_executable, so no macOS
# bundle is created for a test.
function(app_add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE appcore Qt6::Test)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE TEST_APP_NAME="${APP_NAME}")
    app_enable_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Every test has a time limit, so a hang fails within minutes and names
    # the test.
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
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

app_add_unit_test(tst_pagecheck tst_pagecheck.cpp)
app_add_unit_test(tst_browserprofiles tst_browserprofiles.cpp support/TestFiles.h)
target_compile_definitions(tst_browserprofiles PRIVATE LOCKHOLDER_PATH="$<TARGET_FILE:lockholder>")
add_dependencies(tst_browserprofiles lockholder)

app_add_unit_test(tst_pagebudget tst_pagebudget.cpp support/TestFiles.h)

app_add_unit_test(tst_lookup tst_lookup.cpp)
target_link_libraries(tst_lookup PRIVATE applookup)

app_add_unit_test(tst_matching tst_matching.cpp support/TestFiles.h)
target_link_libraries(tst_matching PRIVATE applookup)

app_add_unit_test(tst_lookupstore tst_lookupstore.cpp)
target_link_libraries(tst_lookupstore PRIVATE applookup)

# The Qt Quick views on synthetic data, offscreen (QT_QPA_PLATFORM set by
# the test itself).
app_add_unit_test(tst_ui tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE appui appuiplugin)

app_add_unit_test(tst_browserlogic tst_browserlogic.cpp)
target_link_libraries(tst_browserlogic PRIVATE appbrowser)

# Synthetic HTML fixtures, built into the test as :/fixtures/<name>.
set(app_test_fixtures
    fixtures/active_search.html
    fixtures/check_page.html
    fixtures/error_page.html
    fixtures/fewer_words.html
    fixtures/item_dom.html
    fixtures/item_inline.html
    fixtures/item_jsonld.html
    fixtures/late_page.html
    fixtures/research.html
    fixtures/research_empty.html
    fixtures/ship_to.html
    fixtures/ship_to_hidden.html
    fixtures/sold_search.html
    fixtures/webdriver_check.html
    fixtures/zero_results.html
)

# Extractors on fixtures through a real installed browser (skipped, with a
# message, where none is installed).
app_add_unit_test(tst_extractors tst_extractors.cpp support/ProcessList.h)
target_link_libraries(tst_extractors PRIVATE appbrowser)
qt_add_resources(tst_extractors "extractor_fixtures" PREFIX "/fixtures" BASE fixtures FILES ${app_test_fixtures})
