# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Locate librpitx (installed to /usr/local/lib by install.sh)
find_library(RPITX_LIBRARY
    NAMES rpitx
    HINTS /usr/local/lib
)

if(NOT RPITX_LIBRARY)
    message(FATAL_ERROR "librpitx not found in /usr/local/lib. "
                        "Run install.sh first to build and install librpitx!")
endif()

# Common system libraries used with librpitx
set(RPITX_COMMON_LIBS ${RPITX_LIBRARY} m rt pthread)

# Required libraries (installed by install.sh)
find_library(SNDFILE_LIBRARY sndfile REQUIRED)

# ----------------------------------------------------------
# Shared DSP and I/O utility library
# ----------------------------------------------------------
add_library(rpitx_utils STATIC
    utils/agc.cpp
    utils/biquad.cpp
    utils/cli_utils.cpp
    utils/hilbert.cpp
    utils/morse_encoder.cpp
    utils/wav_utils.cpp
)
target_include_directories(rpitx_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils)
target_link_libraries(rpitx_utils PRIVATE m)

# ----------------------------------------------------------
# Define targets for each rpitx-ui component
# ----------------------------------------------------------
# --- freedv ---
add_executable(freedv freedv/freedv.cpp)
target_link_libraries(freedv PRIVATE ${RPITX_COMMON_LIBS})

# --- piam (stdin int16 -> librpitx amdmasync, self-contained) ---
add_executable(piam
    piam/main.cpp
    piam/am_processor.cpp
)
target_link_libraries(piam PRIVATE rpitx_utils ${RPITX_COMMON_LIBS})

# --- pichirp ---
add_executable(pichirp pichirp/main.cpp)
target_link_libraries(pichirp PRIVATE rpitx_utils ${RPITX_COMMON_LIBS})

# --- pifmrds ---
add_library(pifmrds_c_objs OBJECT
    pifmrds/control_pipe.c
    pifmrds/fm_mpx.c
    pifmrds/rds.c
    pifmrds/waveforms.c
)
add_executable(pifmrds
    pifmrds/pi_fm_rds.cpp
    $<TARGET_OBJECTS:pifmrds_c_objs>
)
target_compile_options(pifmrds PRIVATE -Wno-write-strings)
target_link_libraries(pifmrds PRIVATE ${RPITX_COMMON_LIBS} ${SNDFILE_LIBRARY})

# --- pimorse ---
add_executable(pimorse pimorse/main.cpp)
target_link_libraries(pimorse PRIVATE rpitx_utils ${RPITX_COMMON_LIBS})

# --- piopera ---
add_executable(piopera opera/opera.cpp)
target_link_libraries(piopera PRIVATE ${RPITX_COMMON_LIBS})

# --- pirtty ---
add_executable(pirtty pirtty/pirtty.cpp)
target_link_libraries(pirtty PRIVATE ${RPITX_COMMON_LIBS})

# --- pissb (stdin int16 -> stdout float IQ, no buffering) ---
add_executable(pissb
    pissb/main.cpp
    pissb/ssb_processor.cpp
)
target_link_libraries(pissb PRIVATE rpitx_utils)

# --- pisstv ---
add_executable(pisstv sstv/pisstv.cpp)
target_link_libraries(pisstv PRIVATE ${RPITX_COMMON_LIBS})

# --- pocsag ---
add_executable(pocsag pocsag/pocsag.cpp)
target_link_libraries(pocsag PRIVATE ${RPITX_COMMON_LIBS})

# --- rpitx (v1 compat) ---
add_executable(rpitx rpitxv1/rpitx.cpp)
target_compile_options(rpitx PRIVATE -Wno-write-strings)
target_link_libraries(rpitx PRIVATE ${RPITX_COMMON_LIBS})

# --- sendiq ---
add_executable(sendiq sendiq.cpp)
target_link_libraries(sendiq PRIVATE ${RPITX_COMMON_LIBS})

# --- spectrumpaint ---
add_executable(spectrumpaint spectrumpaint/spectrum.cpp)
target_link_libraries(spectrumpaint PRIVATE ${RPITX_COMMON_LIBS})

# --- tune ---
add_executable(tune tune.cpp)
target_link_libraries(tune PRIVATE ${RPITX_COMMON_LIBS})

# ----------------------------------------------------------
# Optional targets (not used by easytest.sh directly)
# ----------------------------------------------------------
if(BUILD_OPTIONAL_TARGETS)
    # --- corel8 ---
    add_executable(corel8 corel8/corel8.cpp)
    target_compile_options(corel8 PRIVATE -Wno-write-strings)
    target_link_libraries(corel8 PRIVATE ${RPITX_COMMON_LIBS})

    # --- dvbrf (ARM32 assembly - only works on 32-bit ARM) ---
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        add_executable(dvbrf
            dvb/dvbrf.cpp
            dvb/dvbs2arm_1v30.s
            dvb/dvbsenco8.s
            dvb/fec100.c
        )
        target_link_libraries(dvbrf PRIVATE ${RPITX_COMMON_LIBS})
    else()
        message(STATUS "Skipping dvbrf target (requires aarch32 platform, current platform: ${CMAKE_SYSTEM_PROCESSOR})")
    endif()

    # --- foxhunt ---
    add_executable(foxhunt foxhunt/foxhunt.cpp)
    target_link_libraries(foxhunt PRIVATE ${RPITX_COMMON_LIBS})

    # --- pidcf77 ---
    add_executable(pidcf77 dcf77/pidcf77.c)
    target_link_libraries(pidcf77 PRIVATE ${RPITX_COMMON_LIBS})

    # --- pifm ---
    add_executable(pifm fm/pifm.c)
    target_link_libraries(pifm PRIVATE ${RPITX_COMMON_LIBS} ${SNDFILE_LIBRARY})

    # --- pifsq ---
    add_executable(pifsq fsq/pifsq.cpp)
    target_link_libraries(pifsq PRIVATE ${RPITX_COMMON_LIBS})

    # --- pift8 ---
    find_library(FT8_LIBRARY ft8 REQUIRED)
    add_executable(pift8 pift8/pift8.cpp)
    target_compile_options(pift8 PRIVATE -Wno-write-strings)
    target_include_directories(pift8 PRIVATE /usr/local/include)
    target_link_libraries(pift8 PRIVATE ${FT8_LIBRARY} ${RPITX_COMMON_LIBS})

    # --- rds_wav ---
    add_executable(rds_wav
        pifmrds/rds_wav.c
        $<TARGET_OBJECTS:pifmrds_c_objs>
    )
    target_link_libraries(rds_wav PRIVATE m ${SNDFILE_LIBRARY})

    # --- sendook ---
    add_executable(sendook ook/sendook.cpp ook/optparse.c)
    target_compile_options(sendook PRIVATE -Wno-write-strings)
    target_link_libraries(sendook PRIVATE ${RPITX_COMMON_LIBS})
else()
    message(STATUS "Optional targets build disabled - skipping targets not used by easytest.sh")
endif()

# ----------------------------------------------------------
# Install compiled binaries to /usr/bin
# ----------------------------------------------------------
# Install required targets
set(INSTALL_TARGETS
    freedv piam pichirp pifmrds pimorse piopera pirtty pissb
    pisstv pocsag rpitx sendiq spectrumpaint tune
)

# Install optional targets if they were built successfully
foreach(OPTIONAL_TARGET corel8 dvbrf foxhunt pidcf77 pifm pifsq pift8 rds_wav sendook)
    if(TARGET ${OPTIONAL_TARGET})
        list(APPEND INSTALL_TARGETS ${OPTIONAL_TARGET})
    endif()
endforeach()

install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin
)

# ----------------------------------------------------------
# Install shell scripts to /usr/bin
# ----------------------------------------------------------
# Main UI entry point (installed as 'rpitx-ui')
install(PROGRAMS ${CMAKE_SOURCE_DIR}/easytest.sh
    DESTINATION bin
    RENAME rpitx-ui
)

# UI-related scripts in the source root
install(PROGRAMS ${CMAKE_SOURCE_DIR}/rtlmenu.sh DESTINATION bin)

if(BUILD_OPTIONAL_TARGETS)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/ft8menu.sh DESTINATION bin)
else()
    message(STATUS "Skipping ft8menu.sh installation (requires pift8 from optional targets)")
endif()

# Helper scripts in the scripts/ subdirectory
set(SCRIPTS_DIR_SCRIPTS
    fm2ssb.sh snap2spectrum.sh snapsstv.sh
    sv1afnfilter.sh testam.sh testchirp.sh testfmrds.sh
    testfreedv.sh testlsb.sh testmorse.sh testnfm.sh testopera.sh
    testpocsag.sh testrtty.sh testspectrum.sh testusb.sh testsstv.sh
    testvfo.sh transponder.sh
)

foreach(SCRIPT ${SCRIPTS_DIR_SCRIPTS})
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/${SCRIPT} DESTINATION bin)
endforeach()

# Helper scripts that depend on optional binaries (foxhunt, pifsq)
if(BUILD_OPTIONAL_TARGETS)
    install(PROGRAMS
        ${CMAKE_SOURCE_DIR}/scripts/testfoxhunt.sh
        ${CMAKE_SOURCE_DIR}/scripts/testfsq.sh
        DESTINATION bin
    )
else()
    message(STATUS "Skipping testfoxhunt.sh installation (requires foxhunt from optional targets)")
    message(STATUS "Skipping testfsq.sh installation (requires pifsq from optional targets)")
endif()

# ----------------------------------------------------------
# Install resource files to /usr/share/rpitx-ui
# ----------------------------------------------------------
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/resources/
    DESTINATION share/rpitx-ui
)
