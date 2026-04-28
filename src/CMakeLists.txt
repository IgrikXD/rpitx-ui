# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared DSP utility library
# ----------------------------------------------------------
add_library(rpitx_dsp_utils STATIC
    utils/dsp/agc.cpp
    utils/dsp/biquad.cpp
    utils/dsp/hilbert.cpp
)
target_include_directories(rpitx_dsp_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils/dsp)
target_link_libraries(rpitx_dsp_utils PRIVATE m)

# ----------------------------------------------------------
# Shared audio utility library
# ----------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

add_library(rpitx_audio_utils STATIC
    utils/audio/audio_pipeline.cpp
    utils/audio/libsndfile_audio_source.cpp
    utils/audio/polyphase_resampler.cpp
)
target_include_directories(rpitx_audio_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils/audio)
target_link_libraries(rpitx_audio_utils PRIVATE PkgConfig::SNDFILE)

# ----------------------------------------------------------
# Shared CLI parsing utility library
# ----------------------------------------------------------
find_package(CLI11 CONFIG REQUIRED)
add_library(rpitx_cli_utils STATIC
    utils/cli/cli_common.cpp
    utils/cli/cli_validators.cpp
)
target_include_directories(rpitx_cli_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils/cli)
target_link_libraries(rpitx_cli_utils PUBLIC CLI11::CLI11)

# ----------------------------------------------------------
# Define targets for each rpitx-ui component
# ----------------------------------------------------------
# --- freedv ---
add_executable(freedv freedv/freedv.cpp)
target_link_libraries(freedv PRIVATE librpitx::librpitx)

# --- piam (audio file -> librpitx amdmasync, self-contained) ---
add_executable(piam
    piam/piam.cpp
    piam/am_processor.cpp
)
target_link_libraries(piam PRIVATE rpitx_audio_utils rpitx_cli_utils librpitx::librpitx)

# --- pichirp ---
add_executable(pichirp pichirp/pichirp.cpp)
target_link_libraries(pichirp PRIVATE rpitx_cli_utils librpitx::librpitx)

# --- pifmrds (audio file -> librpitx ngfmdmasync, self-contained) ---
add_executable(pifmrds
    pifmrds/pifmrds.cpp
    pifmrds/fmrds_processor.cpp
    pifmrds/rds_encoder.cpp
    pifmrds/rds_modulator.cpp
    pifmrds/rds_pulse.cpp
)
target_link_libraries(pifmrds
    PRIVATE
        rpitx_audio_utils
        rpitx_cli_utils
        librpitx::librpitx
)

# --- pimorse ---
add_executable(pimorse
    pimorse/morse_encoder.cpp
    pimorse/pimorse.cpp
)
target_link_libraries(pimorse PRIVATE rpitx_cli_utils librpitx::librpitx)

# --- pinfm (audio file -> librpitx ngfmdmasync, self-contained) ---
add_executable(pinfm
    pinfm/pinfm.cpp
    pinfm/nfm_processor.cpp
)
target_link_libraries(pinfm PRIVATE rpitx_audio_utils rpitx_cli_utils librpitx::librpitx)

# --- piopera ---
add_executable(piopera opera/opera.cpp)
target_link_libraries(piopera PRIVATE librpitx::librpitx)

# --- pirfgen ---
add_executable(pirfgen
    pirfgen/pirfgen.cpp
    pirfgen/rfgen_processor.cpp
    pirfgen/generators/multitone_generator.cpp
    pirfgen/generators/noise_generator.cpp
    pirfgen/generators/sawtooth.cpp
    pirfgen/generators/sweep_generator.cpp
)
target_link_libraries(pirfgen PRIVATE rpitx_cli_utils librpitx::librpitx)

# --- pirtty ---
add_executable(pirtty pirtty/pirtty.cpp)
target_link_libraries(pirtty PRIVATE librpitx::librpitx)

# --- pissb (audio file -> librpitx iqdmasync, self-contained) ---
add_executable(pissb
    pissb/pissb.cpp
    pissb/ssb_processor.cpp
)
target_link_libraries(pissb PRIVATE rpitx_audio_utils rpitx_cli_utils librpitx::librpitx)

# --- pisstv ---
add_executable(pisstv sstv/pisstv.cpp)
target_link_libraries(pisstv PRIVATE librpitx::librpitx)

# --- pocsag ---
add_executable(pocsag pocsag/pocsag.cpp)
target_link_libraries(pocsag PRIVATE librpitx::librpitx)

# --- sendiq ---
add_executable(sendiq sendiq.cpp)
target_link_libraries(sendiq PRIVATE librpitx::librpitx)

# --- spectrumpaint ---
add_executable(spectrumpaint spectrumpaint/spectrum.cpp)
target_link_libraries(spectrumpaint PRIVATE librpitx::librpitx)

# --- tune ---
add_executable(tune tune.cpp)
target_link_libraries(tune PRIVATE librpitx::librpitx)

# ----------------------------------------------------------
# Optional targets (not used by easytest.sh directly)
# ----------------------------------------------------------
if(BUILD_OPTIONAL_TARGETS)
    # --- corel8 ---
    add_executable(corel8 corel8/corel8.cpp)
    target_compile_options(corel8 PRIVATE -Wno-write-strings)
    target_link_libraries(corel8 PRIVATE librpitx::librpitx)

    # --- dvbrf (ARM32 assembly - only works on 32-bit ARM) ---
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        add_executable(dvbrf
            dvb/dvbrf.cpp
            dvb/dvbs2arm_1v30.s
            dvb/dvbsenco8.s
            dvb/fec100.c
        )
        target_link_libraries(dvbrf PRIVATE librpitx::librpitx)
    else()
        message(STATUS "Skipping dvbrf target (requires aarch32 platform, current platform: ${CMAKE_SYSTEM_PROCESSOR})")
    endif()

    # --- foxhunt ---
    add_executable(foxhunt foxhunt/foxhunt.cpp)
    target_link_libraries(foxhunt PRIVATE librpitx::librpitx)

    # --- pidcf77 ---
    add_executable(pidcf77 dcf77/pidcf77.c)
    target_link_libraries(pidcf77 PRIVATE librpitx::librpitx)

    # --- pifsq ---
    add_executable(pifsq fsq/pifsq.cpp)
    target_link_libraries(pifsq PRIVATE librpitx::librpitx)

    # --- pift8 ---
    add_executable(pift8 pift8/pift8.cpp)
    target_compile_options(pift8 PRIVATE -Wno-write-strings)
    target_link_libraries(pift8 PRIVATE ft8::ft8 librpitx::librpitx)

    # --- rpitx (v1 compat) ---
    add_executable(rpitx rpitxv1/rpitx.cpp)
    target_compile_options(rpitx PRIVATE -Wno-write-strings)
    target_link_libraries(rpitx PRIVATE librpitx::librpitx)

    # --- sendook ---
    add_executable(sendook ook/sendook.cpp ook/optparse.c)
    target_compile_options(sendook PRIVATE -Wno-write-strings)
    target_link_libraries(sendook PRIVATE librpitx::librpitx)
else()
    message(STATUS "Optional targets build disabled - skipping targets not used by easytest.sh")
endif()

# ----------------------------------------------------------
# Install compiled binaries to /usr/bin
# ----------------------------------------------------------
# Install required targets
set(INSTALL_TARGETS
    freedv piam pichirp pifmrds pimorse pinfm piopera pirfgen
    pirtty pissb pisstv pocsag sendiq spectrumpaint tune
)

# Install optional targets if they were built successfully
foreach(OPTIONAL_TARGET corel8 dvbrf foxhunt pidcf77 pifsq pift8 rpitx sendook)
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
    fm2ssb.sh snap2spectrum.sh snapsstv.sh sv1afnfilter.sh testam.sh
    testchirp.sh testfmrds.sh testfreedv.sh testmorse.sh
    testnfm.sh testopera.sh testpocsag.sh testrfgen.sh testrtty.sh
    testspectrum.sh testssb.sh testsstv.sh testvfo.sh transponder.sh
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
