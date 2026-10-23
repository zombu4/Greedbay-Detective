# Unit tests (QtTest). Plain add_executable, not qt_add_executable, so no macOS
# bundle is created for a test.
function(app_add_unit_test name)
    add_executable(${name} ${ARGN} support/TestGuard.cpp)
    target_link_libraries(${name} PRIVATE appcore Qt6::Test)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE TEST_APP_NAME="${APP_NAME}")
    app_enable_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Every test has a time limit, so a hang fails within minutes and names
    # the test.
    # Labels: "unit" here; tests that start a real browser are relabelled
    # "browser" below (tools/affected-tests runs those only for browser
    # changes).
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 LABELS unit)
endfunction()

# Helper programs started by tests; built like tests but not run by ctest.
# Tests and helpers get the product identifier as TEST_APP_NAME (a string
# literal), so names and paths in tests follow APP_NAME.
function(app_add_test_helper name)
    add_executable(${name} ${ARGN} support/TestGuard.cpp)
    target_link_libraries(${name} PRIVATE appcore)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE TEST_APP_NAME="${APP_NAME}")
    app_enable_warnings(${name})
endfunction()

app_add_unit_test(tst_version tst_version.cpp)
app_add_unit_test(tst_settings tst_settings.cpp support/TestFiles.h)
app_add_unit_test(tst_pathchecks tst_pathchecks.cpp support/TestFiles.h)
app_add_unit_test(tst_workfolder tst_workfolder.cpp support/TestFiles.h)
# Database backups outside the work folder (core/Backups).
app_add_unit_test(tst_backups tst_backups.cpp support/TestFiles.h)
target_link_libraries(tst_backups PRIVATE Qt6::Sql)
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
app_copy_runtime_dlls(tst_ui)

# Plugins: manifest, install, sandbox, limits, examples; and export files.
# pluginrunner: the plugin runner process, as the app starts itself with
# "--plugin-runner".
app_add_test_helper(pluginrunner helpers/pluginrunner.cpp)
target_link_libraries(pluginrunner PRIVATE appplugins)
app_add_unit_test(tst_plugins tst_plugins.cpp support/TestFiles.h)
target_link_libraries(tst_plugins PRIVATE appplugins Qt6::CorePrivate)
target_compile_definitions(tst_plugins PRIVATE PLUGINRUNNER_PATH="$<TARGET_FILE:pluginrunner>")
add_dependencies(tst_plugins pluginrunner)

# Uninstall of the work folder (core/Uninstall).
app_add_unit_test(tst_uninstall tst_uninstall.cpp support/TestFiles.h)
target_compile_definitions(tst_uninstall PRIVATE LOCKHOLDER_PATH="$<TARGET_FILE:lockholder>")
add_dependencies(tst_uninstall lockholder)

app_add_unit_test(tst_browserlogic tst_browserlogic.cpp)
target_link_libraries(tst_browserlogic PRIVATE appbrowser)
# Browser control without starting a browser, but it looks for the installed
# browsers: labelled "browser" with tst_extractors.
set_tests_properties(tst_browserlogic PROPERTIES LABELS browser)

# Synthetic HTML fixtures, built into the test as :/fixtures/<name>.
set(app_test_fixtures
    fixtures/active_search.html
    fixtures/check_page.html
    fixtures/error_page.html
    fixtures/feedback.html
    fixtures/feedback_pages.html
    fixtures/purchases_pages.html
    fixtures/feedback_partial.html
    fixtures/feedback_ready.html
    fixtures/feedback_stuck.html
    fixtures/fewer_words.html
    fixtures/item_auction.html
    fixtures/item_dom.html
    fixtures/item_inline.html
    fixtures/item_jsonld.html
    fixtures/item_sold.html
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
# The browser-driven suite loads every fixture in a real browser; the feedback
# page cases wait for in-page updates (up to 10 s, twice when a page is read
# again after Continue). CI's macOS runner needed more than 120 s for the
# whole suite (run 39495969553); its own limit is 300 s.
set_tests_properties(tst_extractors PROPERTIES TIMEOUT 300 LABELS browser)

# The shared seller list (DESIGN 3.7): cryptography, trust chain, the server
# (tools/seller-list-server) and the app's client against it, over plain HTTP
# and over the server's own HTTPS. fixtures/tls: two synthetic test chains
# made with openssl for this test only: ca-<n>.pem a test CA (its key was
# discarded), cert-<n>.pem a server certificate for 127.0.0.1 it signed
# (serverAuth, valid 2019-06-01 to 2119-06-01) followed by the CA, as
# Let's Encrypt's fullchain.pem; key-<n>.pem the server certificate's key.
app_add_unit_test(tst_sellerlist tst_sellerlist.cpp)
target_link_libraries(tst_sellerlist PRIVATE sellerserver appsellerlist Qt6::Network)
app_copy_runtime_dlls(tst_sellerlist)
qt_add_resources(tst_sellerlist "tls_fixtures" PREFIX "/tls" BASE fixtures/tls
    FILES fixtures/tls/ca-a.pem fixtures/tls/cert-a.pem fixtures/tls/key-a.pem
          fixtures/tls/ca-b.pem fixtures/tls/cert-b.pem fixtures/tls/key-b.pem)

# Paging of the purchase check and the review read, without a browser.
app_add_unit_test(tst_sellerpaging tst_sellerpaging.cpp)
target_link_libraries(tst_sellerpaging PRIVATE appui)
app_copy_runtime_dlls(tst_sellerpaging)

# The local model (DESIGN 3.6) without a model: guards, prompts, the fake
# runner (recorded answers), the real runner's refusals, the download.
app_add_test_helper(fakemodelrunner helpers/fakemodelrunner.cpp)
app_add_test_helper(modelrunner helpers/modelrunner.cpp)
target_link_libraries(modelrunner PRIVATE appmodel)
app_add_unit_test(tst_model tst_model.cpp)
target_link_libraries(tst_model PRIVATE appmodel)
target_compile_definitions(tst_model PRIVATE FAKE_MODEL_RUNNER="$<TARGET_FILE:fakemodelrunner>"
                                             MODEL_RUNNER="$<TARGET_FILE:modelrunner>")
add_dependencies(tst_model fakemodelrunner modelrunner)
