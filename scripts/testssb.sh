#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pissb --freq <Hz> --audio <path> [--loop] [--sideband usb|lsb]
#   --audio    : any format libsndfile understands; mono/stereo input is
#                downmixed to mono and resampled to 48 kHz internally.
#   --loop     : replay the audio file continuously when requested by the UI.
#   --sideband : usb (Upper Side Band, default) | lsb (Lower Side Band)
FREQ="$1"
AUDIO="$2"
PLAYBACK="${3:-loop}"
SIDEBAND="${4:-usb}"

# Check if loop playback mode is requested
LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="--loop"
fi

sudo pissb --freq "$FREQ" --audio "$AUDIO" $LOOP_FLAG --sideband "$SIDEBAND"
