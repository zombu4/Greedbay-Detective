# Third-party libraries, each from one pinned archive checked by SHA-256
# before it is used (the same rule as the pinned tools in ci.yml):
#
#   libsodium 1.0.22 (ISC)   Ed25519, X25519, sealed boxes, XChaCha20-Poly1305,
#                            Argon2id, BLAKE2b - the shared seller list's
#                            classic half (src/sellerlist).
#                            https://github.com/jedisct1/libsodium/releases/tag/1.0.22-RELEASE
#                            Both archives below were checked once against the
#                            author's minisign key RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3
#                            (doc.libsodium.org/installation): "Signature and
#                            comment signature verified"; their SHA-256 are
#                            pinned here and equal GitHub's recorded digests.
#   liboqs 0.16.0 (MIT)      ML-KEM-768 (FIPS 203) and ML-DSA-65 (FIPS 204) - the
#                            post-quantum half. Its README: "WE DO NOT
#                            CURRENTLY RECOMMEND RELYING ON THIS LIBRARY IN A
#                            PRODUCTION ENVIRONMENT"; the app uses it only in
#                            hybrids where the classic half must hold too.
#   llama.cpp v0.7.0 (MIT)   the local model (src/model); the newest release
#                            not marked pre-release (GitHub API, 2026-10-20).
#
# The CMake projects (liboqs, llama.cpp) are built as separate projects
# (ExternalProject) with their own options, once per configuration (Debug /
# Release, so MSVC runtimes match), static, without -march=native
# (OQS_DIST_BUILD: liboqs picks its CPU code at run time; GGML_NATIVE OFF:
# ggml's x86-64 defaults SSE4.2, AVX, AVX2, FMA, F16C and BMI2 - the app
# checks the processor before it uses the engine,
# model::processorSupportsEngine). libsodium has no
# CMake build: configure/make on macOS and Linux (static), the author's
# prebuilt MSVC DLLs on Windows (its static .lib is built with /MT, the app
# uses /MD - builds/msvc/properties/ReleaseLIB.props).

include(ExternalProject)
include(FetchContent)
# The C compiler is handed to the C projects (liboqs, llama.cpp's ggml).
enable_language(C)
find_package(Threads REQUIRED)

set(APP_DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps")

# --- libsodium -------------------------------------------------------------

if(WIN32)
    FetchContent_Declare(sodium_msvc
        URL "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22-msvc.zip"
        URL_HASH SHA256=3e03a726fac4bc09cb61d8f29d658ef7a5eca0811de59082130414f7ca2e4279
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(sodium_msvc)
    set(sodium_root "${sodium_msvc_SOURCE_DIR}")
    add_library(app_sodium SHARED IMPORTED GLOBAL)
    set_target_properties(app_sodium PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_RELEASE "${sodium_root}/x64/Release/v143/dynamic/libsodium.dll"
        IMPORTED_IMPLIB_RELEASE "${sodium_root}/x64/Release/v143/dynamic/libsodium.lib"
        IMPORTED_LOCATION_DEBUG "${sodium_root}/x64/Debug/v143/dynamic/libsodium.dll"
        IMPORTED_IMPLIB_DEBUG "${sodium_root}/x64/Debug/v143/dynamic/libsodium.lib"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
        INTERFACE_INCLUDE_DIRECTORIES "${sodium_root}/include")
    set(APP_SODIUM_DLL_RELEASE "${sodium_root}/x64/Release/v143/dynamic/libsodium.dll")
    set(APP_SODIUM_DLL_DEBUG "${sodium_root}/x64/Debug/v143/dynamic/libsodium.dll")
else()
    set(sodium_prefix "${APP_DEPS_PREFIX}/sodium")
    set(sodium_cflags "-O2")
    if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        string(APPEND sodium_cflags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    ExternalProject_Add(sodium_build
        URL "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz"
        URL_HASH SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        PREFIX "${APP_DEPS_PREFIX}/sodium-build"
        INSTALL_DIR "${sodium_prefix}"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure "--prefix=<INSTALL_DIR>" --disable-shared --enable-static --with-pic
                          --disable-dependency-tracking "CFLAGS=${sodium_cflags}"
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        # Its own build messages go to log files, shown when a step fails.
        LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
        BUILD_BYPRODUCTS "${sodium_prefix}/lib/libsodium.a")
    file(MAKE_DIRECTORY "${sodium_prefix}/include")
    add_library(app_sodium STATIC IMPORTED GLOBAL)
    set_target_properties(app_sodium PROPERTIES
        IMPORTED_LOCATION "${sodium_prefix}/lib/libsodium.a"
        INTERFACE_INCLUDE_DIRECTORIES "${sodium_prefix}/include")
    add_dependencies(app_sodium sodium_build)
endif()
# Windows: libsodium is a DLL; a program that links it gets it copied next to
# itself after each build (the DLLs of the libraries a target links:
# $<TARGET_RUNTIME_DLLS>, CMake 3.21). Only for targets that link it.
function(app_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

# libsodium's headers: "SODIUM_STATIC" for the static library only.
if(NOT WIN32)
    set_property(TARGET app_sodium APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS SODIUM_STATIC)
endif()

# --- helper: a CMake project built per configuration ----------------------

# app_external_cmake(<name> URL <url> SHA256 <hash> ARGS <cmake args...>)
# Builds the project once per configuration into <deps>/<name>/<config>.
function(app_external_cmake name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "URL;SHA256" "ARGS;LIBS")
    set(prefix "${APP_DEPS_PREFIX}/${name}")
    set(byproducts "")
    foreach(lib IN LISTS arg_LIBS)
        foreach(config Debug Release)
            list(APPEND byproducts "${prefix}/${config}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        endforeach()
    endforeach()
    ExternalProject_Add(${name}_build
        URL "${arg_URL}"
        URL_HASH SHA256=${arg_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        PREFIX "${APP_DEPS_PREFIX}/${name}-build"
        # "|" in an argument stands for ";" (a CMake list for the project).
        LIST_SEPARATOR |
        CMAKE_ARGS ${arg_ARGS}
                   -DBUILD_SHARED_LIBS=OFF
                   -DCMAKE_INSTALL_LIBDIR=lib
                   -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                   -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                   -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
                   -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                   -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        # One build and install per configuration of the app's build.
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<IF:$<CONFIG:Debug>,Debug,Release>
        INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<IF:$<CONFIG:Debug>,Debug,Release>
                        --prefix "${prefix}/$<IF:$<CONFIG:Debug>,Debug,Release>"
        # The project's own build messages go to log files, shown when a step
        # fails.
        LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
        BUILD_BYPRODUCTS ${byproducts})
    foreach(config Debug Release)
        file(MAKE_DIRECTORY "${prefix}/${config}/include")
    endforeach()
    foreach(lib IN LISTS arg_LIBS)
        add_library(app_${lib} STATIC IMPORTED GLOBAL)
        set_target_properties(app_${lib} PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG "${prefix}/Debug/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            IMPORTED_LOCATION_RELEASE "${prefix}/Release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
            INTERFACE_INCLUDE_DIRECTORIES "${prefix}/$<IF:$<CONFIG:Debug>,Debug,Release>/include")
        add_dependencies(app_${lib} ${name}_build)
    endforeach()
endfunction()

# --- liboqs: only ML-KEM-768 and ML-DSA-65 --------------------------------

# CONFIGURE.md: OQS_MINIMAL_BUILD "Only algorithms explicitly set here are
# included"; OQS_DIST_BUILD "the library will run on any CPU of the target
# architecture"; OQS_USE_OPENSSL OFF (no OpenSSL, no file system scan);
# OQS_BUILD_ONLY_LIB ON (no tests or docs targets).
app_external_cmake(oqs
    URL "https://codeload.github.com/open-quantum-safe/liboqs/tar.gz/refs/tags/0.16.0"
    SHA256 162d5b510518ee5f285f82fa1f16402a885176e818bf1b1a4c3c91c9a2f01eae
    ARGS -DOQS_MINIMAL_BUILD=KEM_ml_kem_768|SIG_ml_dsa_65 -DOQS_DIST_BUILD=ON -DOQS_USE_OPENSSL=OFF
         -DOQS_BUILD_ONLY_LIB=ON
    LIBS oqs)

# --- llama.cpp: the library only (not in a server-only build) -------------

if(APP_SERVER_ONLY)
    return()
endif()

# Its CMakeLists: LLAMA_BUILD_COMMON/TESTS/TOOLS/EXAMPLES/SERVER/APP off (the
# app writes its own grammars), LLAMA_OPENSSL off (no network); ggml:
# GGML_NATIVE off (portable), GGML_OPENMP off (no extra runtime to ship),
# GGML_BLAS off, Metal on macOS with the shader library embedded
# (GGML_METAL_EMBED_LIBRARY follows GGML_METAL).
set(llama_libs llama ggml ggml-base ggml-cpu)
set(llama_args -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_EXAMPLES=OFF
               -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_APP=OFF -DLLAMA_OPENSSL=OFF -DLLAMA_BUILD_UI=OFF
               -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_BLAS=OFF)
if(APPLE)
    list(APPEND llama_libs ggml-metal)
    list(APPEND llama_args -DGGML_METAL=ON)
else()
    list(APPEND llama_args -DGGML_METAL=OFF)
endif()
app_external_cmake(llama
    URL "https://codeload.github.com/ggml-org/llama.cpp/tar.gz/refs/tags/v0.7.0"
    SHA256 55d67acf3ad2ad747a9bbeb75f9f0e173dbcc0d1753a471038a90c819421c5e1
    ARGS ${llama_args}
    LIBS ${llama_libs})
# The static libraries in link order: llama -> ggml -> backends -> ggml-base.
set_property(TARGET app_llama APPEND PROPERTY INTERFACE_LINK_LIBRARIES app_ggml)
set_property(TARGET app_ggml APPEND PROPERTY INTERFACE_LINK_LIBRARIES app_ggml-cpu)
if(APPLE)
    set_property(TARGET app_ggml APPEND PROPERTY INTERFACE_LINK_LIBRARIES app_ggml-metal)
    set_property(TARGET app_ggml-metal APPEND PROPERTY INTERFACE_LINK_LIBRARIES app_ggml-base
                 "-framework Metal" "-framework Foundation" "-framework MetalKit")
    # ggml-cpu links Accelerate when found (GGML_ACCELERATE defaults to ON).
    set_property(TARGET app_ggml-cpu APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-framework Accelerate")
endif()
set_property(TARGET app_ggml-cpu APPEND PROPERTY INTERFACE_LINK_LIBRARIES app_ggml-base)
set_property(TARGET app_ggml-base APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
add_library(app_llama_all INTERFACE)
target_link_libraries(app_llama_all INTERFACE app_llama)
