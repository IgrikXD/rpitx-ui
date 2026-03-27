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
find_library(FT8_LIBRARY ft8 REQUIRED)

# ----------------------------------------------------------
# Define targets for each rpitx-ui component
# ----------------------------------------------------------
# --- corel8 ---
add_executable(corel8 corel8/corel8.cpp)
target_compile_options(corel8 PRIVATE -Wno-write-strings)
target_link_libraries(corel8 PRIVATE ${RPITX_COMMON_LIBS})

# --- foxhunt ---
add_executable(foxhunt foxhunt/foxhunt.cpp)
target_link_libraries(foxhunt PRIVATE ${RPITX_COMMON_LIBS})

# --- freedv ---
add_executable(freedv freedv/freedv.cpp)
target_link_libraries(freedv PRIVATE ${RPITX_COMMON_LIBS})

# --- morse ---
add_executable(morse morse/morse.cpp)
target_link_libraries(morse PRIVATE ${RPITX_COMMON_LIBS})

# --- pichirp ---
add_executable(pichirp chirp/chirp.cpp)
target_link_libraries(pichirp PRIVATE ${RPITX_COMMON_LIBS})

# --- pifsq ---
add_executable(pifsq fsq/pifsq.cpp)
target_link_libraries(pifsq PRIVATE ${RPITX_COMMON_LIBS})

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

# --- pift8 ---
add_executable(pift8 pift8/pift8.cpp)
target_compile_options(pift8 PRIVATE -Wno-write-strings)
target_include_directories(pift8 PRIVATE /usr/local/include)
target_link_libraries(pift8 PRIVATE ${FT8_LIBRARY} ${RPITX_COMMON_LIBS})

# --- piopera ---
add_executable(piopera opera/opera.cpp)
target_link_libraries(piopera PRIVATE ${RPITX_COMMON_LIBS})

# --- pirtty ---
add_executable(pirtty pirtty/pirtty.cpp)
target_link_libraries(pirtty PRIVATE ${RPITX_COMMON_LIBS})

# --- pisstv ---
add_executable(pisstv sstv/pisstv.cpp)
target_link_libraries(pisstv PRIVATE ${RPITX_COMMON_LIBS})

# --- pocsag ---
add_executable(pocsag pocsag/pocsag.cpp)
target_link_libraries(pocsag PRIVATE ${RPITX_COMMON_LIBS})

# --- rds_wav ---
add_executable(rds_wav
    pifmrds/rds_wav.c
    $<TARGET_OBJECTS:pifmrds_c_objs>
)
target_link_libraries(rds_wav PRIVATE m ${SNDFILE_LIBRARY})

# --- rpitx (v1 compat) ---
add_executable(rpitx rpitxv1/rpitx.cpp)
target_compile_options(rpitx PRIVATE -Wno-write-strings)
target_link_libraries(rpitx PRIVATE ${RPITX_COMMON_LIBS})

# --- sendiq ---
add_executable(sendiq sendiq.cpp)
target_link_libraries(sendiq PRIVATE ${RPITX_COMMON_LIBS})

# --- sendook ---
add_executable(sendook ook/sendook.cpp ook/optparse.c)
target_compile_options(sendook PRIVATE -Wno-write-strings)
target_link_libraries(sendook PRIVATE ${RPITX_COMMON_LIBS})

# --- spectrumpaint ---
add_executable(spectrumpaint spectrumpaint/spectrum.cpp)
target_link_libraries(spectrumpaint PRIVATE ${RPITX_COMMON_LIBS})

# --- tune ---
add_executable(tune tune.cpp)
target_link_libraries(tune PRIVATE ${RPITX_COMMON_LIBS})

# ----------------------------------------------------------
# Optional targets (not used by shell scripts directly)
# ----------------------------------------------------------
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
    message(STATUS "Non-ARM platform - skipping dvbrf target (requires ARM assembly)")
endif()

# --- pidcf77 ---
add_executable(pidcf77 dcf77/pidcf77.c)
target_link_libraries(pidcf77 PRIVATE ${RPITX_COMMON_LIBS})

# --- piam ---
add_executable(piam am/piam.c)
target_link_libraries(piam PRIVATE ${RPITX_COMMON_LIBS} ${SNDFILE_LIBRARY})

# --- pifm ---
add_executable(pifm fm/pifm.c)
target_link_libraries(pifm PRIVATE ${RPITX_COMMON_LIBS} ${SNDFILE_LIBRARY})

# --- pissb (stdin int16 -> stdout float IQ, no buffering) ---
add_executable(pissb ssbgen/ssb_stream.c)
target_link_libraries(pissb PRIVATE m)

# --- testssb ---
add_executable(testssb ssbgen/test_ssb.c ssbgen/ssb_gen.c)
target_link_libraries(testssb PRIVATE ${SNDFILE_LIBRARY} m)

# ----------------------------------------------------------
# Install compiled binaries to /usr/bin
# ----------------------------------------------------------
# Install required targets
set(INSTALL_TARGETS 
    corel8 foxhunt freedv morse pichirp pifsq pifmrds pift8 
    piopera pirtty pissb pisstv pocsag rpitx sendiq sendook 
    spectrumpaint tune
)

# Install optional targets if they were built successfully
foreach(OPTIONAL_TARGET dvbrf piam pidcf77 pifm rds_wav testssb)
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
set(ROOT_SCRIPTS ft8menu.sh rtlmenu.sh)

foreach(SCRIPT ${ROOT_SCRIPTS})
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/${SCRIPT} DESTINATION bin)
endforeach()

# Helper scripts in the scripts/ subdirectory
set(SCRIPTS_DIR_SCRIPTS
    fm2ssb.sh snap2spectrum.sh snapsstv.sh
    sv1afnfilter.sh testam.sh testchirp.sh testfmrds.sh testfoxhunt.sh
    testfreedv.sh testfsq.sh testlsb.sh testnfm.sh testopera.sh testpocsag.sh
    testrtty.sh testspectrum.sh testusb.sh testsstv.sh testvfo.sh
    transponder.sh
)

foreach(SCRIPT ${SCRIPTS_DIR_SCRIPTS})
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/${SCRIPT} DESTINATION bin)
endforeach()

# ----------------------------------------------------------
# Install resource files to /usr/share/rpitx-ui
# ----------------------------------------------------------
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/resources/
    DESTINATION share/rpitx-ui
)
