#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 18.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: piopera --freq <Hz> --callsign <callsign> --mode <name>
#   --callsign : letters are sent in upper case, a callsign the Opera code cannot carry is refused.
#   --mode     : 05 (default) | 1 | 2 | 4 | 8 | 16 | 32 - the Opera mode.
FREQ="$1"
CALLSIGN="$2"
MODE="${3:-05}"

sudo piopera --freq "$FREQ" --callsign "$CALLSIGN" --mode "$MODE"
