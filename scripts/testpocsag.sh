#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 16.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testpocsag.sh <freq_Hz> <address> <message> [type] [mode] [function]
# Invokes: pipocsag --freq <Hz> --address <address> --type <type> --mode <rate> [--message <text>] [--function <letter>]
#   --address  : pager address (RIC) in decimal notation.
#   --type     : tone-only | numeric | alphanumeric (default).
#   --mode     : 512 | 1200 (default) | 2400 - the bit rate of the POCSAG mode.
#   --message  : passed when a message is given; a tone-only call has none.
#   --function : a | b | c | d - without it pipocsag chooses the function of the message type.
FREQ="$1"
ADDRESS="$2"
MESSAGE="$3"
TYPE="${4:-alphanumeric}"
MODE="${5:-1200}"
FUNCTION="$6"

# Bash arrays keep the optional arguments properly quoted and only present when they are given.
MESSAGE_FLAG=()
if [ -n "$MESSAGE" ]; then
  MESSAGE_FLAG=(--message "$MESSAGE")
fi
FUNCTION_FLAG=()
if [ -n "$FUNCTION" ]; then
  FUNCTION_FLAG=(--function "$FUNCTION")
fi

sudo pipocsag --freq "$FREQ" --address "$ADDRESS" --type "$TYPE" --mode "$MODE" "${MESSAGE_FLAG[@]}" "${FUNCTION_FLAG[@]}"
