# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 25.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared Morse code utility library
# ----------------------------------------------------------
# Provides the Morse code of the Morse-keying binaries: the ITU table of the
# characters (letters, digits, space and punctuation), the conversion of a
# character into the on-off keying symbols of its dits, dahs and gaps, and the
# length of a symbol at a speed in words per minute.
# ----------------------------------------------------------
add_library(rpitx_morse_utils STATIC
    morse_encoder.cpp
)
target_include_directories(rpitx_morse_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
