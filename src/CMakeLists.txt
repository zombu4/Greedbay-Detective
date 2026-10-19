# appcore: work folder, settings, logging, single-instance lock, version, the
# database layer, eBay URL rules and the client side of the browser worker.
# appcore never links Qt WebEngine: the app process starts no browser.
qt_add_library(appcore STATIC
    core/BrowserProtocol.cpp
    core/BrowserProtocol.h
    core/BrowserWorker.cpp
    core/BrowserWorker.h
    core/Logging.cpp
    core/Logging.h
    core/PathChecks.h
    core/Settings.cpp
    core/Settings.h
    core/SingleInstance.cpp
    core/SingleInstance.h
    core/WebEngineData.cpp
    core/WebEngineData.h
    core/WorkFolder.cpp
    core/WorkFolder.h
    db/Database.cpp
    db/Database.h
    db/Migrations.cpp
    db/Migrations.h
    db/SqlScript.cpp
    db/SqlScript.h
    ebay/PageCheck.cpp
    ebay/PageCheck.h
    ebay/PageKind.cpp
    ebay/PageKind.h
)

configure_file(core/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/core/Version.h" @ONLY)

target_include_directories(appcore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(appcore PUBLIC Qt6::Core Qt6::Network Qt6::Sql)

app_enable_warnings(appcore)

# The schema migrations ship inside the binary as :/migrations/NNNN_name.sql.
qt_add_resources(appcore "migrations"
    PREFIX "/migrations"
    BASE "db/migrations"
    FILES
        db/migrations/0001_init.sql
)

# appbrowser: the Qt WebEngine browser session and the page extractors. Linked
# only into the browser worker and the tests that load pages.
qt_add_library(appbrowser STATIC
    browser/BrowserIdentity.cpp
    browser/BrowserIdentity.h
    browser/BrowserPage.cpp
    browser/BrowserPage.h
    browser/BrowserSession.cpp
    browser/BrowserSession.h
    browser/BrowserWindow.cpp
    browser/BrowserWindow.h
    browser/Extractor.cpp
    browser/Extractor.h
    browser/WorkerHost.cpp
    browser/WorkerHost.h
)

target_link_libraries(appbrowser PUBLIC appcore Qt6::Widgets Qt6::WebEngineCore Qt6::WebEngineWidgets)

app_enable_warnings(appbrowser)

# The extractors run inside pages; they ship as :/extractors/<name>.js.
qt_add_resources(appbrowser "extractors"
    PREFIX "/extractors"
    BASE "browser/extractors"
    FILES
        browser/extractors/common.js
        browser/extractors/item.js
        browser/extractors/research.js
        browser/extractors/search.js
)

# The browser worker process (<APP_NAME>Browser). WIN32: a Windows GUI program,
# so starting it opens no console window. Not a macOS bundle.
function(app_add_browser_worker target output_name)
    qt_add_executable(${target} WIN32 browser/worker_main.cpp)
    # QT_WINDOWS_APP_PROJECT_IDENTIFIER: the identity in the Windows manifest
    # Qt generates (that manifest also declares Windows 10/11 support, so the
    # browser reports the real Windows version in its user agent).
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        QT_WINDOWS_APP_PROJECT_IDENTIFIER "${APP_BUNDLE_ID}.${output_name}")
    target_link_libraries(${target} PRIVATE appbrowser)
    if(WIN32)
        # CommandLineToArgvW (the Unicode command line) is in shell32.
        target_link_libraries(${target} PRIVATE shell32)
    endif()
    app_enable_warnings(${target})
endfunction()

app_add_browser_worker(browserworker "${APP_NAME}Browser")

# The same worker built for tests only: it may also open local fixture pages
# (file: URLs and http://127.0.0.1). Never installed or shipped.
app_add_browser_worker(browserworker_test "${APP_NAME}BrowserTest")
target_compile_definitions(browserworker_test PRIVATE APP_BROWSER_ALLOW_LOCAL_TEST_PAGES)
