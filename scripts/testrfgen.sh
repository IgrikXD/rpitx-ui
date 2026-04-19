#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 14.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testrfgen.sh <freq_Hz> <bandwidth_Hz> <mode> [tones]
#   <mode>    noise | sweep | multitone
#   [tones]   Tone count for multitone mode (default 8, ignored in other modes)
TONES_FLAG=()
if [ "$3" = "multitone" ]; then
	TONES_FLAG=(-t "${4:-8}")
fi

sudo pirfgen "$1" "$2" -m "$3" "${TONES_FLAG[@]}"
