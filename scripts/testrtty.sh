#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 14.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pirtty --freq <Hz> --message <text> --mode <name>
#   --message : letters are sent in upper case, characters ITA2 does not have are skipped.
#   --mode    : 45 (45.45 baud, default) | 50 | 75 - all with a 170 Hz shift.
FREQ="$1"
MESSAGE="$2"
MODE="${3:-45}"

sudo pirtty --freq "$FREQ" --message "$MESSAGE" --mode "$MODE"
