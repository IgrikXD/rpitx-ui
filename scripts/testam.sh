#!/bin/sh

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

# POSIX sh has no array support; branch on playback mode so quoting around
# --audio is preserved while --loop is conditionally appended.
if [ "$PLAYBACK" = "loop" ]; then
  sudo piam --freq "$FREQ" --audio "$AUDIO" --loop
else
  sudo piam --freq "$FREQ" --audio "$AUDIO"
fi
