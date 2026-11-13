#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 13.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pisstv --freq <Hz> --image <path> --mode <name> [--loop]
#   --image : binary PPM converted from the selected picture by ImageMagick;
#             the picture is scaled to fit the picture size of the SSTV mode
#             and centred on a black background.
#   --mode  : martin-m1 (default) | martin-m2 | scottie-s1 | scottie-s2 |
#             robot-36 | robot-72 | pd-90 | pd-120 | pd-180
#   --loop  : retransmit the picture continuously.
FREQ="$1"
IMAGE="$2"
PLAYBACK="${3:-loop}"
MODE="${4:-martin-m1}"

# Check if loop playback mode is requested
LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="--loop"
fi

# Picture size of the SSTV mode
case "$MODE" in
  robot-*) PICTURE_SIZE=320x240 ;;
  pd-120 | pd-180) PICTURE_SIZE=640x496 ;;
  *) PICTURE_SIZE=320x256 ;;
esac

# Converted picture - removed when the script exits
PPM_IMAGE=$(mktemp --suffix=.ppm)
trap 'rm -f "$PPM_IMAGE"' EXIT

convert "$IMAGE" -resize "$PICTURE_SIZE" -background black -gravity center -extent "$PICTURE_SIZE" \
  -depth 8 "ppm:${PPM_IMAGE}" || exit 1
sudo pisstv --freq "$FREQ" --image "$PPM_IMAGE" --mode "$MODE" $LOOP_FLAG
