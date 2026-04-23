# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

include(ExternalProject)

find_program(MAKE_EXECUTABLE NAMES make REQUIRED)

set(FT8_LIB_INSTALL_DIR "${THIRD_PARTY_INSTALL_DIR}/ft8_lib")
set(FT8_LIB_LIBRARY "${FT8_LIB_INSTALL_DIR}/lib/libft8.a")

# Minimal object set required by pift8 (transitive closure of its includes).
# text.o is pulled in because pack.o references symbols from it.
set(FT8_LIB_OBJECTS
    "<SOURCE_DIR>/common/wave.o"
    "<SOURCE_DIR>/ft8/constants.o"
    "<SOURCE_DIR>/ft8/encode.o"
    "<SOURCE_DIR>/ft8/pack.o"
    "<SOURCE_DIR>/ft8/text.o"
)

# Public headers consumed by pift8 directly.
set(FT8_LIB_COMMON_HEADERS
    "<SOURCE_DIR>/common/wave.h"
)
set(FT8_LIB_FT8_HEADERS
    "<SOURCE_DIR>/ft8/constants.h"
    "<SOURCE_DIR>/ft8/encode.h"
    "<SOURCE_DIR>/ft8/pack.h"
)

file(MAKE_DIRECTORY
    "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/common"
    "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/ft8"
    "${FT8_LIB_INSTALL_DIR}/lib"
)

# Build the gen_ft8 Makefile target rather than `all`: its prerequisites are
# exactly the .o files we need, so the decoder, FFT and tests are skipped.
# `make all` does not produce libft8.a, so the archive is assembled manually.
ExternalProject_Add(ft8_lib
    PREFIX "${THIRD_PARTY_DIR}/ft8_lib"
    GIT_REPOSITORY "https://github.com/F5OEO/ft8_lib"
    GIT_TAG "91f2e648c8755d717177586675262310862bc0a8"
    # We pin a known-good commit because upstream is effectively unmaintained.
    # UPDATE_DISCONNECTED avoids automatic update attempts against the remote.
    UPDATE_DISCONNECTED TRUE
    SOURCE_DIR "${THIRD_PARTY_SOURCE_DIR}/ft8_lib"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>" gen_ft8
    BUILD_BYPRODUCTS ${FT8_LIB_OBJECTS}
    INSTALL_COMMAND
        "${CMAKE_COMMAND}" -E copy
            ${FT8_LIB_COMMON_HEADERS}
            "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/common/"
        COMMAND "${CMAKE_COMMAND}" -E copy
            ${FT8_LIB_FT8_HEADERS}
            "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/ft8/"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${FT8_LIB_LIBRARY}"
        COMMAND "${CMAKE_AR}" rc "${FT8_LIB_LIBRARY}" ${FT8_LIB_OBJECTS}
        COMMAND "${CMAKE_RANLIB}" "${FT8_LIB_LIBRARY}"
    INSTALL_BYPRODUCTS "${FT8_LIB_LIBRARY}"
)

add_library(ft8::ft8 STATIC IMPORTED GLOBAL)
set_target_properties(ft8::ft8 PROPERTIES
    IMPORTED_LOCATION "${FT8_LIB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FT8_LIB_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "m"
)
add_dependencies(ft8::ft8 ft8_lib)
