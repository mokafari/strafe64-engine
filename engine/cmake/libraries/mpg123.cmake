if(NOT USE_CODEC_MP3)
    return()
endif()

if(NOT BUILD_CLIENT)
    return()
endif()

# libmpg123 decodes MP3 background music. Always an external dependency —
# there is no internal copy. Install on macOS with `brew install mpg123`.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)

list(APPEND CLIENT_LIBRARIES PkgConfig::MPG123)
list(APPEND CLIENT_DEFINITIONS USE_CODEC_MP3)
