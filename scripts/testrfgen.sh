#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testrfgen.sh <freq_Hz> <bandwidth_Hz> <sample_rate_Hz> <mode> [tones]
#   <mode>    noise | sweep | multitone
#   [tones]   Tone count for multitone mode (required for multitone, rejected otherwise)
FREQ="$1"
BANDWIDTH="$2"
SAMPLE_RATE="$3"
MODE="$4"
TONES="${5:-8}"

# Bash array keeps optional --tone-count properly quoted and only present for
# multitone; pirfgen rejects --tone-count for noise/sweep modes.
TONE_FLAG=()
if [ "$MODE" = "multitone" ]; then
  TONE_FLAG=(--tone-count "$TONES")
fi

sudo pirfgen --freq "$FREQ" --bandwidth "$BANDWIDTH" --sample-rate "$SAMPLE_RATE" --mode "$MODE" "${TONE_FLAG[@]}"
