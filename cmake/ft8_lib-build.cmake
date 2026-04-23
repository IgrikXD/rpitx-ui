# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

include(FetchContent)

set(FT8_LIB_SOURCE_DIR "${THIRD_PARTY_SOURCE_DIR}/ft8_lib")

FetchContent_Declare(ft8_lib
    GIT_REPOSITORY "https://github.com/F5OEO/ft8_lib"
    GIT_TAG "91f2e648c8755d717177586675262310862bc0a8"
    SOURCE_DIR "${FT8_LIB_SOURCE_DIR}"
    BINARY_DIR "${THIRD_PARTY_BUILD_DIR}/ft8_lib"
)
FetchContent_MakeAvailable(ft8_lib)

add_library(ft8_lib STATIC
    "${FT8_LIB_SOURCE_DIR}/ft8/constants.cpp"
    "${FT8_LIB_SOURCE_DIR}/ft8/encode.cpp"
    "${FT8_LIB_SOURCE_DIR}/ft8/pack.cpp"
    "${FT8_LIB_SOURCE_DIR}/ft8/text.cpp"
    "${FT8_LIB_SOURCE_DIR}/common/wave.cpp"
)

target_include_directories(ft8_lib
    PUBLIC
        "${THIRD_PARTY_SOURCE_DIR}"
    PRIVATE
        "${FT8_LIB_SOURCE_DIR}"
        "${FT8_LIB_SOURCE_DIR}/common"
        "${FT8_LIB_SOURCE_DIR}/ft8"
)
target_link_libraries(ft8_lib PUBLIC m)
set_target_properties(ft8_lib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${THIRD_PARTY_BUILD_DIR}/ft8_lib"
)

add_library(ft8::ft8 ALIAS ft8_lib)
