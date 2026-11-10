#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 09.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pispectrumpaint --freq <Hz> --bandwidth <Hz> --image <path>
#   A 320x256 snapshot is taken with the camera tool of the installed OS
#   (rpicam-still, libcamera-still or the legacy raspistill), converted into a
#   binary PGM by ImageMagick and painted once; see testspectrum.sh for the
#   conversion flags. The bandwidth is 100 kHz.
FREQ="$1"
BANDWIDTH=100000

# Snapshot and converted picture - removed when the script exits
SNAPSHOT=$(mktemp --suffix=.jpg)
PGM_IMAGE=$(mktemp --suffix=.pgm)
trap 'rm -f "$SNAPSHOT" "$PGM_IMAGE"' EXIT

# Pick the camera tool shipped with the installed Raspberry Pi OS release
for CAMERA_TOOL in rpicam-still libcamera-still raspistill; do
  if command -v "$CAMERA_TOOL" > /dev/null 2>&1; then
    break
  fi
  CAMERA_TOOL=""
done
if [ -z "$CAMERA_TOOL" ]; then
  echo "[ERROR] No camera tool found (rpicam-still, libcamera-still or raspistill)!" >&2
  exit 1
fi

"$CAMERA_TOOL" -n -t 1 --width 320 --height 256 -o "$SNAPSHOT" || exit 1
convert "$SNAPSHOT" -flip -colorspace Gray -depth 8 "pgm:${PGM_IMAGE}" || exit 1
sudo pispectrumpaint --freq "$FREQ" --bandwidth "$BANDWIDTH" --image "$PGM_IMAGE"
