# Top-level build.
#
# APP_NAME is the only place the product name is set, and project() below is
# the only place the version is set. Targets, the version header and the work
# folder name all derive from these two values, so renaming the product is a
# one-line change.
cmake_minimum_required(VERSION 4.4)

set(APP_NAME "AppName")

project(${APP_NAME}
    VERSION 0.1.0
    DESCRIPTION "Price checker for eBay listings"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/warnings.cmake)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Sql Test)
qt_standard_project_setup()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
