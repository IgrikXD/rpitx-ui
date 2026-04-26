#!/bin/sh

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 26.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testfmrds.sh <freq_Hz> <audio.wav> [pi] [ps] [rt] [pe]
#   pi : RDS PI, 1-4 hex digits  (default 1234)
#   ps : RDS PS, up to 8 chars   (default rpitx-ui)
#   rt : RDS RT, up to 64 chars  (default rpitx-ui Broadcast WFM with RDS)
#   pe : Pre-emphasis us, 50|75  (default 50)
#   Input audio: 16-bit PCM, mono or stereo, any sample rate.
PI="${3:-1234}"
PS="${4:-rpitx-ui}"
RT="${5:-rpitx-ui Broadcast WFM with RDS}"
PE="${6:-50}"
(while true; do cat "$2"; done) | sudo pifmrds "$1" -pi "$PI" -ps "$PS" -rt "$RT" -pe "$PE"
