# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

include(ExternalProject)

find_program(MAKE_EXECUTABLE NAMES make REQUIRED)
find_package(Threads REQUIRED)

set(LIBRPITX_INSTALL_DIR "${THIRD_PARTY_INSTALL_DIR}/librpitx")
set(LIBRPITX_LIBRARY "${LIBRPITX_INSTALL_DIR}/lib/librpitx.a")

file(MAKE_DIRECTORY
    "${LIBRPITX_INSTALL_DIR}/include/librpitx"
    "${LIBRPITX_INSTALL_DIR}/lib"
)

# Build only librpitx.a (skip librpitx.so, which we don't link against) and
# delegate header/archive layout to the upstream `install` target via PREFIX.
ExternalProject_Add(librpitx
    PREFIX "${THIRD_PARTY_DIR}/librpitx"
    GIT_REPOSITORY "https://github.com/F5OEO/librpitx"
    GIT_TAG "f01bdb64bcdb6207f448379193bc0a8accb9aa22"
    # We pin a known-good commit because upstream is effectively unmaintained.
    # UPDATE_DISCONNECTED avoids automatic update attempts against the remote.
    UPDATE_DISCONNECTED TRUE
    SOURCE_DIR "${THIRD_PARTY_SOURCE_DIR}/librpitx"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>/src" librpitx.a
    INSTALL_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>/src" install "PREFIX=${LIBRPITX_INSTALL_DIR}"
    BUILD_BYPRODUCTS "<SOURCE_DIR>/src/librpitx.a"
    INSTALL_BYPRODUCTS "${LIBRPITX_LIBRARY}"
)

add_library(librpitx::librpitx STATIC IMPORTED GLOBAL)
set_target_properties(librpitx::librpitx PROPERTIES
    IMPORTED_LOCATION "${LIBRPITX_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBRPITX_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "m;rt;Threads::Threads"
)
add_dependencies(librpitx::librpitx librpitx)
