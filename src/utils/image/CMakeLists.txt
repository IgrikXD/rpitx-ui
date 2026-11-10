# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 09.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared image utility library
# ----------------------------------------------------------
# Provides the picture front-end shared by the picture-transmitting binaries:
# a streaming reader for binary PGM (P5) grayscale images (PgmReader) that
# accepts a file or stdin, 8- and 16-bit samples, and streams of concatenated
# images, so the binaries parse a trivial text header instead of linking an
# image-decoding library - ImageMagick converts the user's picture up front.
# ----------------------------------------------------------
add_library(rpitx_image_utils STATIC
    pgm_reader.cpp
)
target_include_directories(rpitx_image_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
