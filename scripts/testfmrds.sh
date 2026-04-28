#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testfmrds.sh <freq_Hz> <audio.wav> <loop|once> [pi] [ps] [rt] [pe]
#   loop|once : Replay the audio file continuously, or play it once and stop.
#   pi  : RDS PI, 1-4 hex digits  (default 1234)
#   ps  : RDS PS, 1-8 bytes       (default rpitx-ui)
#   rt  : RDS RT, 1-64 bytes      (default rpitx-ui Broadcast WFM with RDS)
#   pe  : Pre-emphasis us, 50|75  (default 50)
#   Input audio: any format libsndfile understands (WAV / FLAC / OGG / ...).
FREQ="$1"
AUDIO="$2"
PLAYBACK="${3:-loop}"
PI="${4:-1234}"
PS="${5:-rpitx-ui}"
RT="${6:-rpitx-ui Broadcast WFM with RDS}"
PE="${7:-50}"

# POSIX sh has no array support; branch on playback mode so quoting around
# --audio / --rds-* is preserved while --loop is conditionally appended.
if [ "$PLAYBACK" = "loop" ]; then
  sudo pifmrds --freq "$FREQ" --audio "$AUDIO" --loop \
    --rds-pi "$PI" --rds-ps "$PS" --rds-rt "$RT" --pre-emphasis "$PE"
else
  sudo pifmrds --freq "$FREQ" --audio "$AUDIO" \
    --rds-pi "$PI" --rds-ps "$PS" --rds-rt "$RT" --pre-emphasis "$PE"
fi
