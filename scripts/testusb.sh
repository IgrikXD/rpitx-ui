#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

(while true; do cat "$2"; done) | pissb -u \
  | sudo sendiq -i /dev/stdin -s 48000 -f "$1" -t float

