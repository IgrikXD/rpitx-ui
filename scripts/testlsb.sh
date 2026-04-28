#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Pipeline: pissb --sideband lsb | sendiq -i /dev/stdin -s 48000 -f <Hz> -t float
#   Input audio is replayed continuously into pissb, which expects 16-bit
#   PCM/WAV audio at the 48 kHz SSB processing rate.
(while true; do cat "$2"; done) | pissb --sideband lsb \
  | sudo sendiq -i /dev/stdin -s 48000 -f "$1" -t float
