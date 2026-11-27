#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testook.sh <freq_Hz> <message> [type] [symbol_time_us] [repeat] [pause_us]
# Invokes: piook --freq <Hz> --message <sequence> --type <type> [--symbol-time <us>] --repeat <count> --pause <us>
#   --message     : keying sequence of the carrier, a message with anything else is refused.
#   --type        : symbols (default) - a string of 1 and 0 | timings - durations of the pulses and gaps in microseconds.
#   --symbol-time : 500 by default; passed for a message of symbols only (piook refuses it otherwise).
#   --repeat      : number of times the sequence is sent, 1 by default.
#   --pause       : pause between the repetitions in microseconds, none by default.
FREQ="$1"
MESSAGE="$2"
TYPE="${3:-symbols}"
SYMBOL_TIME="${4:-500}"
REPEAT="${5:-1}"
PAUSE="${6:-0}"

# A bash array keeps the optional argument properly quoted and only present for a message of symbols.
SYMBOL_TIME_FLAG=()
if [ "$TYPE" = "symbols" ]; then
  SYMBOL_TIME_FLAG=(--symbol-time "$SYMBOL_TIME")
fi

sudo piook --freq "$FREQ" --message "$MESSAGE" --type "$TYPE" "${SYMBOL_TIME_FLAG[@]}" --repeat "$REPEAT" --pause "$PAUSE"
