if(NOT USE_CODEC_MOD)
    return()
endif()

if(NOT BUILD_CLIENT)
    return()
endif()

# libopenmpt decodes tracker modules (.it/.xm/.s3m/.mod/.mptm). Always an
# external dependency — there is no internal copy. Install on macOS with
# `brew install libopenmpt`.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENMPT REQUIRED IMPORTED_TARGET libopenmpt)

list(APPEND CLIENT_LIBRARIES PkgConfig::OPENMPT)
list(APPEND CLIENT_DEFINITIONS USE_CODEC_MOD)
