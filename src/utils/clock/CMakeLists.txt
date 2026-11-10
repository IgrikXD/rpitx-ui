# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 10.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared clock utility library
# ----------------------------------------------------------
# Provides the setup of the carrier PLL shared by the transmitters that
# program it themselves: the PLL divider is chosen for the whole modulated
# band, and the settings librpitx would program although the board cannot
# run them are refused before the PLL is programmed for the band.
# ----------------------------------------------------------
add_library(rpitx_clock_utils STATIC
    carrier_pll.cpp
)
target_include_directories(rpitx_clock_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_clock_utils PUBLIC librpitx::librpitx)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
