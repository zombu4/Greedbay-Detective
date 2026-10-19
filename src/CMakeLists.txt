# appcore: work folder, settings, logging, single-instance lock, version, the
# database layer, browser profiles, the daily page count and the eBay URL rules.
qt_add_library(appcore STATIC
    core/BrowserProfiles.cpp
    core/BrowserProfiles.h
    core/Logging.cpp
    core/Logging.h
    core/PageBudget.cpp
    core/PageBudget.h
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
    ebay/PageCheck.cpp
    ebay/PageCheck.h
    ebay/PageKind.cpp
    ebay/PageKind.h
    ebay/SearchUrls.cpp
    ebay/SearchUrls.h
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

# appbrowser: drives the user's installed browser over the DevTools protocol
# and holds the page extractors.
qt_add_library(appbrowser STATIC
    browser/BrowserCatalog.cpp
    browser/BrowserCatalog.h
    browser/BrowserController.cpp
    browser/BrowserController.h
    browser/CdpConnection.cpp
    browser/CdpConnection.h
    browser/Extractor.cpp
    browser/Extractor.h
)

target_link_libraries(appbrowser PUBLIC appcore Qt6::Network Qt6::WebSockets)
if(APPLE)
    # Launch Services finds installed browsers by bundle identifier.
    target_link_libraries(appbrowser PRIVATE "-framework CoreServices" "-framework CoreFoundation")
endif()

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

# applookup: one lookup (item page -> identity -> queries -> comparison pages
# -> records -> report). Browser-free: it asks for pages through a callback,
# so tests run it on fake page records.
qt_add_library(applookup STATIC
    lookup/Identity.cpp
    lookup/Identity.h
    lookup/LookupReport.cpp
    lookup/LookupReport.h
    lookup/LookupRunner.cpp
    lookup/LookupRunner.h
    lookup/Records.cpp
    lookup/Records.h
    lookup/Stats.cpp
    lookup/Stats.h
)

target_link_libraries(applookup PUBLIC appcore)

app_enable_warnings(applookup)
