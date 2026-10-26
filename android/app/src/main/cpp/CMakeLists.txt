cmake_minimum_required(VERSION 3.22.1)
project(simutrans_android LANGUAGES C CXX)

# ===========================================================================
# Phase 2: the real game.
#
#   libSDL2.so      shared, built from source (same pin as the official
#                   simutrans-android-project)
#   libz            NDK system library (sysroot ships zlib for android-24)
#   libbz2.a        bzip2 1.0.8 vendored at cpp/bzip2 (static)
#   libpng16.a      libpng 1.6.44 via its own CMakeLists (static)
#   libfreetype.a   freetype 2.13.3 via its own CMakeLists (static)
#   libmain.so      the ENTIRE simutrans tree (repo root CMakeLists.txt,
#                   ANDROID branch) — exports SDL_main, launched by
#                   SDLActivity; the diagnostics bridge (diag.cc) is
#                   compiled into it and hooked from simsys_s2.cc.
#
# Everything except SDL2 is linked STATICALLY into libmain.so: no extra
# .so files to package, no soname juggling, no load order.
#
# The upstream file cmake/SimutransFindDependencies.cmake is patched to skip
# its Android "prebuilt sibling dir" import block when the ZLIB::ZLIB target
# already exists (we create it below). Logged in docs/PORT_NOTES.md.
# ===========================================================================

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The Gradle "debug" variant only controls signing; we still want a playable,
# optimized software renderer. CMake's default Debug flags carry no -O level,
# i.e. clang defaults to -O0 => unplayable at 1440x3200. Force -O2 globally
# (SDL2, libpng, freetype and the game itself).
set(CMAKE_C_FLAGS_DEBUG       "-g -O2"                       CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG     "-g -O2 -fno-limit-debug-info" CACHE STRING "" FORCE)

include(FetchContent)

# --- SDL2 (shared) — same pin as the official simutrans-android-project -------
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/archive/6ad5a966ea95918a4534ce22a7b31387db70675a.tar.gz
    URL_HASH SHA256=d4fd734572f5f5100b6c85916b1af4027c87029ce5fe6560a89d93fdeb3a4a69
)
set(SDL_SHARED   ON  CACHE BOOL "" FORCE)
set(SDL_STATIC   OFF CACHE BOOL "" FORCE)
# Software-rendering policy (spec): no GL/EGL/Vulkan code paths from us.
# SDL still compiles its own optional render backends but we never use them.
FetchContent_MakeAvailable(SDL2)

# --- zlib: NDK system library --------------------------------------------------
# The sysroot ships zlib.h + libz.so for android-24. Providing the ZLIB::ZLIB
# target here (a) gives the game its zlib and (b) makes the patched
# cmake/SimutransFindDependencies.cmake skip the prebuilt-import block.
# libpng's own find_package(ZLIB) also sees the existing target.
add_library(ZLIB::ZLIB INTERFACE IMPORTED)
target_link_libraries(ZLIB::ZLIB INTERFACE z)

# --- bzip2 1.0.8 (vendored, static) --------------------------------------------
add_subdirectory(bzip2)
add_library(BZip2::BZip2 ALIAS bz2_static)

# --- libpng 1.6.44 (static via upstream CMakeLists; links NDK system zlib) -----
set(PNG_SHARED     OFF CACHE BOOL "" FORCE)
set(PNG_STATIC     ON  CACHE BOOL "" FORCE)
set(PNG_TESTS      OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(libpng
    URL https://downloads.sourceforge.net/project/libpng/libpng16/1.6.44/libpng-1.6.44.tar.xz
    URL_HASH SHA256=60c4da1d5b7f0aa8d158da48e8f8afa9773c1c8baa5d21974df61f1886b8ce8e
)
FetchContent_MakeAvailable(libpng)
add_library(PNG::PNG ALIAS png_static)

# --- freetype 2.13.3 (static; all optional deps disabled) -----------------------
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB     TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   TRUE CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    URL https://downloads.sourceforge.net/project/freetype/freetype2/2.13.3/freetype-2.13.3.tar.xz
    URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
)
FetchContent_MakeAvailable(freetype)
add_library(Freetype::Freetype ALIAS freetype)

# --- the game itself -------------------------------------------------------------
# Repo root = five levels up from this file
# (cpp -> main -> src -> app -> android -> <repo root>)
set(SIMUTRANS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
if(NOT EXISTS "${SIMUTRANS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "simutrans source tree not found at ${SIMUTRANS_SOURCE_DIR}")
endif()

set(SIMUTRANS_BACKEND         "sdl2" CACHE STRING "" FORCE)
set(SIMUTRANS_MULTI_THREAD    OFF    CACHE BOOL   "" FORCE)
set(SIMUTRANS_USE_ZSTD        OFF    CACHE BOOL   "" FORCE)
set(SIMUTRANS_USE_FONTCONFIG  OFF    CACHE BOOL   "" FORCE)
set(SIMUTRANS_USE_UPNP        OFF    CACHE BOOL   "" FORCE)
set(SIMUTRANS_WARNINGS_AS_ERRORS OFF CACHE BOOL   "" FORCE)
# Deterministic revision label (avoids git-detection at build time; offline
# single-player: the revision number is a label only).
set(SIMUTRANS_USE_REVISION    "1"    CACHE STRING "" FORCE)

add_subdirectory("${SIMUTRANS_SOURCE_DIR}" simutrans-build)

# libmain.so: SDLActivity loads the "main" library and finds SDL_main in it
set_target_properties(simutrans PROPERTIES OUTPUT_NAME main)

# Diagnostics bridge compiled into the game library (hooks in simsys_s2.cc)
target_sources(simutrans PRIVATE diag.cc)
target_include_directories(simutrans PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Defensive include dirs for the static deps (in case their targets do not
# propagate PUBLIC includes): png.h/pnglibconf.h, ft2build.h
target_include_directories(simutrans PRIVATE
    ${libpng_SOURCE_DIR}
    ${libpng_BINARY_DIR}
    ${freetype_SOURCE_DIR}/include
    ${freetype_BINARY_DIR}/include
)
