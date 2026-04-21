#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 20.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testam.sh <freq_Hz> <audio.wav>
#   Input audio must be 16-bit PCM mono at 48 kHz.
(while true; do cat "$2"; done) | sudo piam "$1"
