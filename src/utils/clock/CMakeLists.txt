# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 10.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared clock utility library
# ----------------------------------------------------------
# Provides the carrier PLL setup shared by the transmitters: the settings
# librpitx would program although the board cannot run them are refused
# before the transmitter engine is constructed, the transmitters that
# program the PLL themselves get its divider chosen for the whole modulated
# band, and the frequency samples of an FM engine are delivered finer than
# the frequency step of the PLL (PllStepFeedback, header-only).
# ----------------------------------------------------------
add_library(rpitx_clock_utils STATIC
    carrier_pll.cpp
)
target_include_directories(rpitx_clock_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_clock_utils PUBLIC librpitx::librpitx)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
