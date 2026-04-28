#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 28.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Usage: testmorse.sh <freq_Hz> <WPM> <message>
#   The message must be quoted at the call site if it contains spaces.
sudo pimorse --freq "$1" --wpm "$2" --message "$3"
