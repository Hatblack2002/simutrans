# bzip2 1.0.8 vendored for the android-port build (static, PIC).
# Source: https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
# sha256: ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
# Only the library sources are vendored (no bzip2/bunzip2 tools); the
# upstream tarball has no CMake build, so we provide this minimal one.
add_library(bz2_static STATIC
    blocksort.c
    bzlib.c
    compress.c
    crctable.c
    decompress.c
    huffman.c
    randtable.c
)
target_include_directories(bz2_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
