# Installers (PLAN 9; owner decisions: .dmg, setup .exe and .deb, unsigned).
# `cpack` in a release build folder makes the one of this system:
#   macOS    DragNDrop: the app bundle and a link to /Applications in a .dmg
#            (https://cmake.org/cmake/help/latest/cpack_gen/dmg.html)
#   Windows  NSIS: a per-user setup .exe from packaging/windows/NSIS.template.in
#   Linux    DEB: the app under APP_LINUX_INSTALL_PREFIX with its Qt
#            libraries, and a desktop entry
# Only the app is installed: not the developer window, not the tests. The
# Qt libraries, plugins and QML modules it uses go with it
# (qt_generate_deploy_qml_app_script); the IBM Plex fonts are inside the
# program (src/CMakeLists.txt, "fonts").

if(APPLE)
    install(TARGETS ${APP_NAME} BUNDLE DESTINATION .)
else()
    # bin and lib (GNUInstallDirs): qt_standard_project_setup adds $ORIGIN
    # and "$ORIGIN/<the path from CMAKE_INSTALL_BINDIR to
    # CMAKE_INSTALL_LIBDIR>" to the install RPATH on Linux, so the program
    # finds the Qt libraries deployed next to it (Qt docs).
    install(TARGETS ${APP_NAME} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# The app speaks English only (owner decision Q5): no Qt translations.
# Windows: the compiler runtime comes from InstallRequiredSystemLibraries
# below (owner decision Q16: the Visual C++ runtime DLLs next to the
# program), not from the deploy tool.
qt_generate_deploy_qml_app_script(
    TARGET ${APP_NAME}
    OUTPUT_SCRIPT app_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
)
install(SCRIPT "${app_deploy_script}")

if(MSVC)
    # "searches for compiler-provided system runtime libraries and adds
    # installation rules for them"; the destination defaults to bin on
    # Windows, where the program is (InstallRequiredSystemLibraries docs).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    include(InstallRequiredSystemLibraries)
endif()

# "values of variables prefixed with CPACK_ will be escaped before being
# written to the configuration files, so that the cpack program receives them
# exactly as they were specified" (CMake's CPack module): the NSIS values below
# hold backslashes and quotes.
set(CPACK_VERBATIM_VARIABLES TRUE)

set(CPACK_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "zombu4")
set(CPACK_PACKAGE_CONTACT "zombu4 <227538759+zombu4@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${APP_NAME}")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${APP_NAME}")
set(CPACK_PACKAGE_EXECUTABLES "${APP_NAME}" "${APP_DISPLAY_NAME}")
# greedbay-detective-0.1.0-macos-arm64.dmg and so on.
if(APPLE)
    set(app_package_system "macos-arm64")
elseif(WIN32)
    set(app_package_system "windows-x64")
else()
    set(app_package_system "linux-x64")
endif()
set(CPACK_PACKAGE_FILE_NAME "${APP_PACKAGE_NAME}-${PROJECT_VERSION}-${app_package_system}")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "${APP_DISPLAY_NAME}")
    set(CPACK_DMG_FORMAT "UDZO")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")
    # The project's own template (per-user, the uninstall of the user's
    # data). CPack looks for NSIS.template.in on its module path before its
    # own (cmCPackGenerator::FindTemplate); CPack.cmake hands that path to
    # cpack as CPACK_MODULE_PATH (default: CMAKE_MODULE_PATH).
    set(CPACK_MODULE_PATH "${PROJECT_SOURCE_DIR}/packaging/windows")
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\Programs")
    set(CPACK_NSIS_PACKAGE_NAME "${APP_DISPLAY_NAME}")
    set(CPACK_NSIS_DISPLAY_NAME "${APP_DISPLAY_NAME}")
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "${APP_NAME}.exe")
    set(CPACK_NSIS_DEFINES
        "  !define APP_EXE \"${APP_NAME}.exe\"\n  !define APP_SHOWN \"${APP_DISPLAY_NAME}\"\n  !define APP_DATA \"${APP_NAME}\"")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB")
    # The desktop entry goes to /usr/share/applications, outside the
    # program's prefix: CPACK_SET_DESTDIR "may help (or simply be necessary)
    # if some install rules uses absolute DESTINATION" (CMake docs); CPack
    # then packages CMAKE_INSTALL_PREFIX (APP_LINUX_INSTALL_PREFIX).
    set(CPACK_SET_DESTDIR ON)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/app.desktop.in" "${PROJECT_BINARY_DIR}/${APP_PACKAGE_NAME}.desktop" @ONLY)
    install(FILES "${PROJECT_BINARY_DIR}/${APP_PACKAGE_NAME}.desktop" DESTINATION /usr/share/applications)
    # Dependencies from the binaries themselves: dpkg-shlibdeps, which CPack
    # runs with the $ORIGIN folders of the package as private library folders
    # and --ignore-missing-info (CMake 4.4 CPackDeb.cmake).
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
endif()

include(CPack)

# The names the CI steps need (package, install test), from this one place:
# <build>/package-info.env, NAME=value lines.
file(WRITE "${PROJECT_BINARY_DIR}/package-info.env"
    "APP_NAME=${APP_NAME}\nAPP_PACKAGE_NAME=${APP_PACKAGE_NAME}\nAPP_LINUX_INSTALL_PREFIX=${APP_LINUX_INSTALL_PREFIX}\n"
    "APP_VERSION=${PROJECT_VERSION}\nAPP_VENDOR=${CPACK_PACKAGE_VENDOR}\n")
