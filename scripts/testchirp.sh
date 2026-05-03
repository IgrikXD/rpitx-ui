#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pichirp --freq <Hz> --bandwidth <Hz> --sweep-time <seconds>
#   Bandwidth and sweep time are fixed by the rpitx-ui menu wrapper:
#   100 kHz RF bandwidth across a 5 s sweep period.
FREQ="$1"
BANDWIDTH=100000
SWEEP_TIME=5

sudo pichirp --freq "$FREQ" --bandwidth "$BANDWIDTH" --sweep-time "$SWEEP_TIME"
