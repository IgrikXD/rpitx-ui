# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

include(ExternalProject)

find_program(MAKE_EXECUTABLE NAMES make REQUIRED)

set(FT8_LIB_INSTALL_DIR "${THIRD_PARTY_INSTALL_DIR}/ft8_lib")
set(FT8_LIB_LIBRARY "${FT8_LIB_INSTALL_DIR}/lib/libft8.a")

file(MAKE_DIRECTORY
    "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/common"
    "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/ft8"
    "${FT8_LIB_INSTALL_DIR}/lib"
)

ExternalProject_Add(ft8_lib
    PREFIX "${THIRD_PARTY_DIR}/ft8_lib"
    GIT_REPOSITORY "https://github.com/F5OEO/ft8_lib"
    GIT_TAG "91f2e648c8755d717177586675262310862bc0a8"
    UPDATE_DISCONNECTED TRUE
    SOURCE_DIR "${THIRD_PARTY_SOURCE_DIR}/ft8_lib"
    BINARY_DIR "${THIRD_PARTY_BUILD_DIR}/ft8_lib"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${MAKE_EXECUTABLE}" -C "<SOURCE_DIR>" all
    INSTALL_COMMAND
        "${CMAKE_COMMAND}" -E make_directory
            "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/common"
            "${FT8_LIB_INSTALL_DIR}/include/ft8_lib/ft8"
            "${FT8_LIB_INSTALL_DIR}/lib"
        COMMAND sh -c
            "cp \"<SOURCE_DIR>\"/common/*.h \"${FT8_LIB_INSTALL_DIR}/include/ft8_lib/common/\""
        COMMAND sh -c
            "cp \"<SOURCE_DIR>\"/ft8/*.h \"${FT8_LIB_INSTALL_DIR}/include/ft8_lib/ft8/\""
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${FT8_LIB_LIBRARY}"
        COMMAND sh -c
            "cd \"<SOURCE_DIR>\" && \"${CMAKE_AR}\" rc \"${FT8_LIB_LIBRARY}\" common/*.o fft/*.o ft8/*.o"
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
