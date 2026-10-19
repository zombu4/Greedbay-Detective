# Top-level build.
#
# APP_NAME is the only place the product name is set, and project() below is
# the only place the version is set. Everything else derives from these two
# values, so renaming the product is a one-line change:
#   APP_NAME          GreedbayDetective   identifier, work folder name, database name
#   APP_DISPLAY_NAME  Greedbay Detective  window titles, Start menu, Apps & features
#   APP_PACKAGE_NAME  greedbay-detective  installer and .deb package names
#   APP_BUNDLE_ID     io.github.zombu4.GreedbayDetective   macOS bundle identifier
cmake_minimum_required(VERSION 4.4)

set(APP_NAME "GreedbayDetective")

# "GreedbayDetective" -> "Greedbay Detective" -> "greedbay-detective"
string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1 \\2" APP_DISPLAY_NAME "${APP_NAME}")
string(TOLOWER "${APP_DISPLAY_NAME}" APP_PACKAGE_NAME)
string(REPLACE " " "-" APP_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(APP_BUNDLE_ID "io.github.zombu4.${APP_NAME}")

project(${APP_NAME}
    VERSION 0.1.0
    DESCRIPTION "Price checker for eBay listings"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/warnings.cmake)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Network Sql Test WebSockets Widgets)
qt_standard_project_setup()

enable_testing()

add_subdirectory(src)
add_subdirectory(tools/devwindow)
add_subdirectory(tests)
