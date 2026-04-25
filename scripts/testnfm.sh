#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 24.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testnfm.sh <freq_Hz> <audio.wav> [mode]
#   mode: narrow (+-2.5 kHz, 12.5 kHz channels) | wide (+-5 kHz, 25 kHz channels, default)
#   Input audio must be 16-bit PCM mono at 48 kHz.
MODE="${3:-wide}"
(while true; do cat "$2"; done) | sudo pinfm "$1" -m "$MODE"
