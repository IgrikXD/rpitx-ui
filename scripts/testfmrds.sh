#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testfmrds.sh <freq_Hz> <audio.wav> <loop|once> [pi] [ps] [rt] [pe]
#   loop|once : Replay the audio file continuously, or play it once and stop.
#   pi  : RDS PI, 1-4 hex digits  (default 1234)
#   ps  : RDS PS, up to 8 chars   (default rpitx-ui)
#   rt  : RDS RT, up to 64 chars  (default rpitx-ui Broadcast WFM with RDS)
#   pe  : Pre-emphasis us, 50|75  (default 50)
#   Input audio: any format libsndfile understands (WAV / FLAC / OGG / ...).
PLAYBACK="${3:-loop}"
PI="${4:-1234}"
PS="${5:-rpitx-ui}"
RT="${6:-rpitx-ui Broadcast WFM with RDS}"
PE="${7:-50}"

LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="-l"
fi

sudo pifmrds "$1" -a "$2" $LOOP_FLAG -pi "$PI" -ps "$PS" -rt "$RT" -pe "$PE"
