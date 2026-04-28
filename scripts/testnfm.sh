#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testnfm.sh <freq_Hz> <audio.wav> <loop|once> [mode]
#   loop|once : Replay the audio file continuously, or play it once and stop.
#   mode      : narrow (+-2.5 kHz, 12.5 kHz channels) | wide (+-5 kHz, 25 kHz channels, default)
#   Input audio: any format libsndfile understands (downmixed to mono and
#                resampled to 48 kHz internally).
FREQ="$1"
AUDIO="$2"
PLAYBACK="${3:-loop}"
MODE="${4:-wide}"

# POSIX sh has no array support; branch on playback mode so quoting around
# --audio is preserved while --loop is conditionally appended.
if [ "$PLAYBACK" = "loop" ]; then
  sudo pinfm --freq "$FREQ" --audio "$AUDIO" --loop --mode "$MODE"
else
  sudo pinfm --freq "$FREQ" --audio "$AUDIO" --mode "$MODE"
fi
