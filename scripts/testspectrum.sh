#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 09.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pispectrumpaint --freq <Hz> --bandwidth <Hz> --image <path> [--loop]
#   --image : binary PGM converted from the selected picture by ImageMagick;
#             pictures larger than 320x256 are scaled down, and the picture is
#             flipped because a waterfall scrolls downwards (the bottom row is
#             painted first).
#   --loop  : repaint the picture continuously.
#   The bandwidth is 100 kHz.
FREQ="$1"
IMAGE="$2"
PLAYBACK="${3:-loop}"
BANDWIDTH=100000

# Check if loop playback mode is requested
LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="--loop"
fi

# Converted picture - removed when the script exits
PGM_IMAGE=$(mktemp --suffix=.pgm)
trap 'rm -f "$PGM_IMAGE"' EXIT

convert "$IMAGE" -flip -resize '320x256>' -colorspace Gray -depth 8 "pgm:${PGM_IMAGE}" || exit 1
sudo pispectrumpaint --freq "$FREQ" --bandwidth "$BANDWIDTH" --image "$PGM_IMAGE" $LOOP_FLAG
