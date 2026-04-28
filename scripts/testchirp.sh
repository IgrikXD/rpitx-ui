#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testchirp.sh <freq_Hz>
#   Bandwidth and sweep time are fixed at the legacy demo defaults
#   (100 kHz across a 5 s period) used by the rpitx-ui menu.
FREQ="$1"
BANDWIDTH=100000
SWEEP_TIME=5

sudo pichirp --freq "$FREQ" --bandwidth "$BANDWIDTH" --sweep-time "$SWEEP_TIME"
