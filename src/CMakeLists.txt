# appcore: work folder, settings, logging, single-instance lock, version, the
# database layer, browser profiles, the daily page count and the eBay URL rules.
qt_add_library(appcore STATIC
    core/BrowserProfiles.cpp
    core/BrowserProfiles.h
    core/ExportFiles.cpp
    core/ExportFiles.h
    core/Logging.cpp
    core/Logging.h
    core/PageBudget.cpp
    core/PageBudget.h
    core/PathChecks.h
    core/Settings.cpp
    core/Settings.h
    core/SingleInstance.cpp
    core/SingleInstance.h
    core/Uninstall.cpp
    core/Uninstall.h
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
        db/migrations/0003_formats.sql
        db/migrations/0004_seller_ratings.sql
        db/migrations/0005_sellers.sql
        db/migrations/0006_model_answers.sql
        db/migrations/0007_locations.sql
)

# A server-only build (APP_SERVER_ONLY, the top-level CMakeLists.txt) needs
# appcore, applookup and appsellerlist only.
if(NOT APP_SERVER_ONLY)
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
        browser/extractors/feedback.js
        browser/extractors/item.js
        browser/extractors/purchases.js
        browser/extractors/research.js
        browser/extractors/search.js
)
endif()

# applookup: one lookup (item page -> identity -> queries -> comparison pages
# -> records -> matching -> value), and saving lookups in the database
# (LookupStore). Browser-free: it asks for pages through a callback, so tests
# run it on fake page records.
qt_add_library(applookup STATIC
    lookup/AuctionTime.cpp
    lookup/AuctionTime.h
    lookup/History.cpp
    lookup/History.h
    lookup/Identity.cpp
    lookup/Identity.h
    lookup/KnownModels.cpp
    lookup/KnownModels.h
    lookup/LookupExport.cpp
    lookup/LookupExport.h
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
    lookup/SellerStore.cpp
    lookup/SellerStore.h
    lookup/Sellers.cpp
    lookup/Sellers.h
    lookup/Stats.cpp
    lookup/Stats.h
    lookup/Text.cpp
    lookup/Text.h
    lookup/Value.cpp
    lookup/Value.h
)

target_link_libraries(applookup PUBLIC appcore)

app_enable_warnings(applookup)

# appsellerlist: the shared seller list's cryptography and documents (DESIGN
# 3.7), used by the app and by tools/seller-list-server: libsodium and
# liboqs (cmake/deps.cmake).
qt_add_library(appsellerlist STATIC
    sellerlist/Crypto.cpp
    sellerlist/Crypto.h
    sellerlist/Protocol.cpp
    sellerlist/Protocol.h
    sellerlist/Trust.cpp
    sellerlist/Trust.h
    sellerlist/ListClient.cpp
    sellerlist/ListClient.h
)
target_link_libraries(appsellerlist PUBLIC applookup Qt6::Core Qt6::Network PRIVATE app_sodium app_oqs)
# The headers include <sodium.h> only in the .cpp files.
target_include_directories(appsellerlist PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
app_enable_warnings(appsellerlist)

# The matcher's data ships inside the binary as :/lookup/<name>.json.
qt_add_resources(applookup "lookup_data"
    PREFIX "/lookup"
    BASE "lookup/data"
    FILES
        lookup/data/known-models.json
        lookup/data/match-words.json
)

if(APP_SERVER_ONLY)
    return()
endif()

# appplugins: JavaScript plugins (DESIGN 5, PLAN 15): the manifest, the
# catalog of built-in examples and installed plugins, the host that runs the
# enabled ones, and the runner: the separate process in which plugin code
# and plugin .zip files are handled.
qt_add_library(appplugins STATIC
    plugins/PluginCatalog.cpp
    plugins/PluginCatalog.h
    plugins/PluginHost.cpp
    plugins/PluginHost.h
    plugins/PluginManifest.cpp
    plugins/PluginManifest.h
    plugins/PluginRunner.cpp
    plugins/PluginRunner.h
)

target_link_libraries(appplugins PUBLIC appcore applookup Qt6::Qml PRIVATE Qt6::CorePrivate)

app_enable_warnings(appplugins)

# The example plugins, one per type, ship inside the app as
# :/plugins/<id>/manifest.json and main.js (built-in, disabled until the
# user enables them).
qt_add_resources(appplugins "plugin_examples"
    PREFIX "/plugins"
    BASE "plugins/examples"
    FILES
        plugins/examples/example-csv-export/main.js
        plugins/examples/example-csv-export/manifest.json
        plugins/examples/example-few-ratings/main.js
        plugins/examples/example-few-ratings/manifest.json
        plugins/examples/example-no-reproductions/main.js
        plugins/examples/example-no-reproductions/manifest.json
        plugins/examples/example-recent-median/main.js
        plugins/examples/example-recent-median/manifest.json
)

# appui: what the Qt Quick views use (src/ui/*.cpp: the lookup session,
# results, recent lookups, settings) and the views themselves
# (src/ui/qml/*.qml), as the static QML module GreedbayDetective.Ui with its
# plugin appuiplugin (class AppUiPlugin; Q_IMPORT_QML_PLUGIN in the program
# that uses it). The QML files are compiled into the library by qmlcachegen.
qt_add_library(appui STATIC
    ui/Backend.cpp
    ui/Backend.h
    ui/BrowserTask.cpp
    ui/BrowserTask.h
    ui/Fonts.cpp
    ui/Fonts.h
    ui/Format.cpp
    ui/Format.h
    ui/HistoryController.cpp
    ui/HistoryController.h
    ui/LookupSession.cpp
    ui/LookupSession.h
    ui/ModelController.cpp
    ui/ModelController.h
    ui/PhotoProvider.cpp
    ui/PhotoProvider.h
    ui/PluginsController.cpp
    ui/PluginsController.h
    ui/PurchaseCheck.cpp
    ui/PurchaseCheck.h
    ui/RecentModel.cpp
    ui/RecentModel.h
    ui/ResultsController.cpp
    ui/ResultsController.h
    ui/SellerController.cpp
    ui/SellerController.h
    ui/SellerSession.cpp
    ui/SellerSession.h
    ui/SettingsController.cpp
    ui/SettingsController.h
    ui/SharedListController.cpp
    ui/SharedListController.h
)

target_link_libraries(appui PUBLIC appbrowser applookup appplugins appsellerlist appmodel Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
# The QML type registration code that qt_add_qml_module writes includes the
# headers by file name (<Backend.h>).
target_include_directories(appui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ui")

app_enable_warnings(appui)
# MSVC C4702 "unreachable code" is raised inside Qt's own templates that the
# code written by the QML tools and moc instantiates (CI run 39328016064,
# appui only):
# qmetatype.h:514, qvariant.h:792 and :797, qjsengine.h:112 - each an
# `if constexpr` chain whose later return cannot be reached for some types.
# Only that one warning is off, only for this target.
if(MSVC)
    target_compile_options(appui PRIVATE /wd4702)
endif()

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
    ui/qml/NumberTile.qml
    ui/qml/Pill.qml
    ui/qml/PriceBarBox.qml
    ui/qml/PriceChart.qml
    ui/qml/PriceHistoryScreen.qml
    ui/qml/ProgressPanel.qml
    ui/qml/RangeBar.qml
    ui/qml/ResultsScreen.qml
    ui/qml/ReviewSearchesPanel.qml
    ui/qml/RowsTable.qml
    ui/qml/SellerPanel.qml
    ui/qml/SessionOverlay.qml
    ui/qml/SettingsScreen.qml
    ui/qml/StartupError.qml
    ui/qml/StatBox.qml
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
# The license texts of the libraries built into the app (cmake/deps.cmake):
# :/licenses/THIRD-PARTY-NOTICES.txt.
qt_add_resources(appui "library_licenses"
    PREFIX "/licenses"
    BASE "${PROJECT_SOURCE_DIR}/resources/licenses"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/licenses/THIRD-PARTY-NOTICES.txt"
)

# appmodel: the local model (DESIGN 3.6): catalog, prompts and grammars, the
# guards, the answer cache, the download, the runner ("--model-runner") on
# llama.cpp (cmake/deps.cmake) and the app's side of it.
qt_add_library(appmodel STATIC
    model/Guards.cpp
    model/Guards.h
    model/ModelAssist.cpp
    model/ModelAssist.h
    model/ModelCatalog.cpp
    model/ModelCatalog.h
    model/ModelClient.cpp
    model/ModelClient.h
    model/ModelDownload.cpp
    model/ModelDownload.h
    model/ModelStore.cpp
    model/ModelStore.h
    model/Prompts.cpp
    model/Prompts.h
    model/Runner.cpp
    model/Runner.h
)
target_link_libraries(appmodel PUBLIC applookup Qt6::Core Qt6::Network Qt6::Sql PRIVATE app_llama_all)
target_include_directories(appmodel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
app_enable_warnings(appmodel)
# The models' license texts as their repositories have them (shown before a
# download): :/models/<file>.
qt_add_resources(appmodel "model_licenses"
    PREFIX "/models"
    BASE "${PROJECT_SOURCE_DIR}/resources/models"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/models/Qwen2.5-1.5B-LICENSE.txt"
        "${PROJECT_SOURCE_DIR}/resources/models/Qwen3-4B-LICENSE.txt"
)

# The shared seller list's owner keys and server host name (DESIGN 3.7), made
# by the list owner with tools/seller-list-server "app-master": by default
# src/sellerlist/data/master.json; APP_SELLERLIST_MASTER names another file
# (a test list). Without the file the app says the list is not set up.
set(APP_SELLERLIST_MASTER "${CMAKE_CURRENT_SOURCE_DIR}/sellerlist/data/master.json"
    CACHE FILEPATH "The shared seller list's master.json")
if(EXISTS "${APP_SELLERLIST_MASTER}")
    get_filename_component(app_master_dir "${APP_SELLERLIST_MASTER}" DIRECTORY)
    get_filename_component(app_master_name "${APP_SELLERLIST_MASTER}" NAME)
    if(NOT app_master_name STREQUAL "master.json")
        message(FATAL_ERROR "APP_SELLERLIST_MASTER must name a file called master.json")
    endif()
    qt_add_resources(appui "sellerlist_master"
        PREFIX "/sellerlist"
        BASE "${app_master_dir}"
        FILES "${APP_SELLERLIST_MASTER}"
    )
endif()

# The app.
qt_add_executable(${APP_NAME} MACOSX_BUNDLE WIN32 app/main.cpp)
target_link_libraries(${APP_NAME} PRIVATE appui appuiplugin)
app_copy_runtime_dlls(${APP_NAME})
set_target_properties(${APP_NAME} PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
target_compile_definitions(${APP_NAME} PRIVATE APP_LINUX_PACKAGE_BIN_DIR="${APP_LINUX_INSTALL_PREFIX}/bin")
if(APPLE)
    # macdeployqt rewrites the program's library paths when it packages the
    # bundle; without room in the header install_name_tool stops with
    # "larger updated load commands do not fit (the program must be relinked,
    # and you may need to use -headerpad or -headerpad_max_install_names)".
    target_link_options(${APP_NAME} PRIVATE "-Wl,-headerpad_max_install_names")
endif()
app_enable_warnings(${APP_NAME})
