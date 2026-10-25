cmake_minimum_required(VERSION 3.22.1)
project(simutrans_android LANGUAGES C CXX)

# --- SDL2, pinned to the exact commit the official simutrans-android-project
#     ships as its submodule (SDL2 branch, 2.33.0-dev line). Tarball hash verified.
include(FetchContent)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/archive/6ad5a966ea95918a4534ce22a7b31387db70675a.tar.gz
    URL_HASH SHA256=d4fd734572f5f5100b6c85916b1af4027c87029ce5fe6560a89d93fdeb3a4a69
)
set(SDL_SHARED   ON  CACHE BOOL "" FORCE)
set(SDL_STATIC   OFF CACHE BOOL "" FORCE)
# Software-rendering policy (spec): no GL/EGL/Vulkan code paths from us.
# SDL still compiles its own optional render backends but we never use them.
FetchContent_MakeAvailable(SDL2)

# --- our native library: libmain.so exporting SDL_main ---
add_library(main SHARED
    main.cc
    diag.cc
)
target_link_libraries(main PRIVATE
    SDL2::SDL2
    log
)
# Android 15+ 16 KiB page alignment (also required for NDK r28+ on arm64)
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")
