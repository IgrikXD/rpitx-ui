#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 10.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Invokes: pitune --freq <Hz>
#   An unmodulated carrier at the requested frequency.
FREQ="$1"

sudo pitune --freq "$FREQ"
