#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testfsq.sh <freq_Hz> <callsign> <message> [mode] [type] [destination]
# Invokes: pifsq --freq <Hz> --callsign <callsign> --message <text> --mode <name> --type <type> [--to <callsign>]
#   --callsign : callsign of the sender, sent as it is written.
#   --message  : a message with a character outside printable ASCII is refused.
#   --mode     : 2 (default) | 3 | 4.5 | 6 - the nominal baud rate of the FSQ mode.
#   --type     : directed (default) | undirected.
#   --to       : passed with a directed message; callsign of its destination, allcall (default) for all stations.
FREQ="$1"
CALLSIGN="$2"
MESSAGE="$3"
MODE="${4:-2}"
TYPE="${5:-directed}"
DESTINATION="${6:-allcall}"

# A bash array keeps the optional argument properly quoted and only present for a directed message.
DESTINATION_FLAG=()
if [ "${TYPE,,}" != "undirected" ]; then
  DESTINATION_FLAG=(--to "$DESTINATION")
fi

sudo pifsq --freq "$FREQ" --callsign "$CALLSIGN" --message "$MESSAGE" --mode "$MODE" --type "$TYPE" "${DESTINATION_FLAG[@]}"
