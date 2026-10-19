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
        db/migrations/0002_saved_lookups.sql
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
# -> records -> matching -> value), and saving lookups in the database
# (LookupStore). Browser-free: it asks for pages through a callback, so tests
# run it on fake page records.
qt_add_library(applookup STATIC
    lookup/Identity.cpp
    lookup/Identity.h
    lookup/KnownModels.cpp
    lookup/KnownModels.h
    lookup/LookupReport.cpp
    lookup/LookupReport.h
    lookup/LookupRunner.cpp
    lookup/LookupRunner.h
    lookup/LookupStore.cpp
    lookup/LookupStore.h
    lookup/MatchWords.cpp
    lookup/MatchWords.h
    lookup/Matcher.cpp
    lookup/Matcher.h
    lookup/Records.cpp
    lookup/Records.h
    lookup/Stats.cpp
    lookup/Stats.h
    lookup/Text.cpp
    lookup/Text.h
    lookup/Value.cpp
    lookup/Value.h
)

target_link_libraries(applookup PUBLIC appcore)

app_enable_warnings(applookup)

# The matcher's data ships inside the binary as :/lookup/<name>.json.
qt_add_resources(applookup "lookup_data"
    PREFIX "/lookup"
    BASE "lookup/data"
    FILES
        lookup/data/known-models.json
        lookup/data/match-words.json
)

# appui: what the Qt Quick views use (src/ui/*.cpp: the lookup session,
# results, recent lookups, settings) and the views themselves
# (src/ui/qml/*.qml), as the static QML module GreedbayDetective.Ui with its
# plugin appuiplugin (class AppUiPlugin; Q_IMPORT_QML_PLUGIN in the program
# that uses it). The QML files are compiled into the library by qmlcachegen.
qt_add_library(appui STATIC
    ui/Backend.cpp
    ui/Backend.h
    ui/Fonts.cpp
    ui/Fonts.h
    ui/Format.cpp
    ui/Format.h
    ui/LookupSession.cpp
    ui/LookupSession.h
    ui/PhotoProvider.cpp
    ui/PhotoProvider.h
    ui/RecentModel.cpp
    ui/RecentModel.h
    ui/ResultsController.cpp
    ui/ResultsController.h
    ui/SettingsController.cpp
    ui/SettingsController.h
)

target_link_libraries(appui PUBLIC appbrowser applookup Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
# The generated QML type registration includes the headers by file name
# (<Backend.h>).
target_include_directories(appui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ui")

app_enable_warnings(appui)

set(app_qml_files
    ui/qml/AppButton.qml
    ui/qml/AppHeader.qml
    ui/qml/AppTextField.qml
    ui/qml/AskModelPanel.qml
    ui/qml/BrowserPromptPanel.qml
    ui/qml/Card.qml
    ui/qml/FlagCard.qml
    ui/qml/HomeScreen.qml
    ui/qml/IdentityEditor.qml
    ui/qml/KeyNumber.qml
    ui/qml/LinkText.qml
    ui/qml/Main.qml
    ui/qml/Pill.qml
    ui/qml/PriceHistoryScreen.qml
    ui/qml/ProgressPanel.qml
    ui/qml/RangeBar.qml
    ui/qml/ResultsScreen.qml
    ui/qml/RowsTable.qml
    ui/qml/SessionOverlay.qml
    ui/qml/SettingsScreen.qml
    ui/qml/StartupError.qml
    ui/qml/Theme.qml
)
# Each view sits at the module's top level (qrc:/qt/qml/GreedbayDetective/Ui/<name>.qml).
foreach(file IN LISTS app_qml_files)
    get_filename_component(name "${file}" NAME)
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
set_source_files_properties(ui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appui
    URI GreedbayDetective.Ui
    VERSION 1.0
    PLUGIN_TARGET appuiplugin
    CLASS_NAME AppUiPlugin
    # Qt's QML tooling expects the module's build folder to end in its URI.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/GreedbayDetective/Ui"
    QML_FILES ${app_qml_files}
)

# The IBM Plex fonts (resources/fonts/README.md: release, checksums) as
# :/fonts/<file>, and their license as :/licenses/IBM-Plex-LICENSE.txt.
qt_add_resources(appui "fonts"
    PREFIX "/fonts"
    BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexMono-Medium.otf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexMono-Regular.otf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-Medium.otf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-Regular.otf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-SemiBold.otf"
)
qt_add_resources(appui "licenses"
    PREFIX "/licenses"
    BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/fonts/IBM-Plex-LICENSE.txt"
)

# The app.
qt_add_executable(${APP_NAME} MACOSX_BUNDLE WIN32 app/main.cpp)
target_link_libraries(${APP_NAME} PRIVATE appui appuiplugin)
set_target_properties(${APP_NAME} PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
app_enable_warnings(${APP_NAME})
