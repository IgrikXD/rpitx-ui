# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

include(ExternalProject)

find_program(MAKE_EXECUTABLE NAMES make REQUIRED)

set(LIBRPITX_INSTALL_DIR "${THIRD_PARTY_INSTALL_DIR}/librpitx")
set(LIBRPITX_LIBRARY "${LIBRPITX_INSTALL_DIR}/lib/librpitx.a")

file(MAKE_DIRECTORY
    "${LIBRPITX_INSTALL_DIR}/include/librpitx"
    "${LIBRPITX_INSTALL_DIR}/lib"
)

ExternalProject_Add(librpitx
    PREFIX "${THIRD_PARTY_DIR}/librpitx"
    GIT_REPOSITORY "https://github.com/F5OEO/librpitx"
    GIT_TAG "f01bdb64bcdb6207f448379193bc0a8accb9aa22"
    SOURCE_DIR "${THIRD_PARTY_SOURCE_DIR}/librpitx"
    BINARY_DIR "${THIRD_PARTY_BUILD_DIR}/librpitx"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>/src" librpitx.a
    INSTALL_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>/src" install "PREFIX=${LIBRPITX_INSTALL_DIR}"
    BUILD_BYPRODUCTS "${THIRD_PARTY_SOURCE_DIR}/librpitx/src/librpitx.a"
    INSTALL_BYPRODUCTS "${LIBRPITX_LIBRARY}"
)

add_library(rpitx::librpitx STATIC IMPORTED GLOBAL)
set_target_properties(rpitx::librpitx PROPERTIES
    IMPORTED_LOCATION "${LIBRPITX_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBRPITX_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES "m;rt;pthread"
)
add_dependencies(rpitx::librpitx librpitx)
