#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testam.sh <freq_Hz> <audio.wav> <loop|once>
#   loop|once : Replay the audio file continuously, or play it once and stop.
#   Input audio: any format libsndfile understands (downmixed to mono and
#                resampled to 48 kHz internally).
FREQ="$1"
AUDIO="$2"
PLAYBACK="${3:-loop}"

# Optional fixed flag; keep unquoted in the command so an empty value vanishes.
LOOP_FLAG=""
if [ "$PLAYBACK" = "loop" ]; then
  LOOP_FLAG="--loop"
fi

sudo piam --freq "$FREQ" --audio "$AUDIO" $LOOP_FLAG
