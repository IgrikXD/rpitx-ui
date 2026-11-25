#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 25.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pifoxhunt --freq <Hz> --code <code> --mode <mode> --wpm <value> [--callsign <callsign>]
#   --code     : MO (finish beacon, default) | MOE | MOI | MOS | MOH | MO5 - transmitters 1 - 5.
#   --mode     : A1A (keyed carrier, default) | A2A (keyed carrier switched by a 1000 Hz tone).
#   --wpm      : keying speed in words per minute, 8 (default) - 15.
#   --callsign : passed when a callsign is given; DE and the callsign end every minute of operation.
FREQ="$1"
CODE="${2:-MO}"
MODE="${3:-A1A}"
WPM="${4:-8}"
CALLSIGN="$5"

# A bash array keeps the optional argument properly quoted and only present when it is given.
CALLSIGN_FLAG=()
if [ -n "$CALLSIGN" ]; then
  CALLSIGN_FLAG=(--callsign "$CALLSIGN")
fi

sudo pifoxhunt --freq "$FREQ" --code "$CODE" --mode "$MODE" --wpm "$WPM" "${CALLSIGN_FLAG[@]}"
