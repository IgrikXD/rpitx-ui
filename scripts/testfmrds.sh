#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pifmrds --freq <Hz> --audio <path> [--loop]
#                  [--rds-pi <hex>] [--rds-ps <text>] [--rds-rt <text>]
#                  [--pre-emphasis 50|75]
#   --audio        : any format libsndfile understands; mono/stereo input is
#                    resampled to the 228 kHz FM MPX rate internally.
#   --loop         : replay the audio file continuously when requested by the UI.
#   --rds-pi       : RDS PI, 1-4 hex digits  (default 1234)
#   --rds-ps       : RDS PS, 1-8 bytes       (default rpitx-ui)
#   --rds-rt       : RDS RT, 1-64 bytes      (default rpitx-ui Broadcast WFM with RDS)
#   --pre-emphasis : FM pre-emphasis in us, 50|75 (default 50)
FREQ="$1"
AUDIO="$2"
PLAYBACK="${3:-loop}"
PI="${4:-1234}"
PS="${5:-rpitx-ui}"
RT="${6:-rpitx-ui Broadcast WFM with RDS}"
PE="${7:-50}"

# Check if loop playback mode is requested
LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="--loop"
fi

sudo pifmrds --freq "$FREQ" --audio "$AUDIO" $LOOP_FLAG \
  --rds-pi "$PI" --rds-ps "$PS" --rds-rt "$RT" --pre-emphasis "$PE"
