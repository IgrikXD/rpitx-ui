#!/bin/bash

# rpitx-ui package version
PACKAGE_VERSION='1.3'

# Terminal color helpers (ANSI escape sequences)
COLOR_GREEN=$'\033[32m'
COLOR_YELLOW=$'\033[33m'
COLOR_BLUE=$'\033[34m'
COLOR_RESET=$'\033[0m'

# Status message helpers
INFO="${COLOR_YELLOW}[INFO]${COLOR_RESET}"
ACTION="${COLOR_BLUE}[ACTION REQUIRED]${COLOR_RESET}"
SEPARATOR='----------------------------------------------------'

# Print a colored banner: print_banner <color_var> <message>
print_banner() {
  local color=$1
  local message=$2
  echo "${color}${SEPARATOR}${COLOR_RESET}"
  echo "${color}${message}${COLOR_RESET}"
  echo "${color}${SEPARATOR}${COLOR_RESET}"
}

# ----------------------------------------------------------
# Installation script entry point
# ----------------------------------------------------------
# Exit immediately if a command exits with a non-zero status
set -e

print_banner "$COLOR_GREEN" "Installing rpitx-ui-${PACKAGE_VERSION}!"

# System dependency installation via package manager
print_banner "$COLOR_YELLOW" 'Installing system dependencies...'
sudo apt update
sudo apt install -y \
  buffer \
  cmake \
  imagemagick \
  libfftw3-dev \
  libsndfile1-dev \
  rtl-sdr
print_banner "$COLOR_YELLOW" 'System dependencies installed successfully!'

# rpitx-ui dependencies installation from source in an independent subshell
# Build dependencies in a temporary directory to keep the source tree clean
BUILD_TMPDIR=$(mktemp -d)
trap 'rm -rf "${BUILD_TMPDIR}"' EXIT

print_banner "$COLOR_YELLOW" 'csdr installation...'
(
  cd "${BUILD_TMPDIR}"
  git clone https://github.com/F5OEO/csdr
  cd csdr
  make && sudo make install
)
print_banner "$COLOR_YELLOW" 'csdr installed successfully!'

print_banner "$COLOR_YELLOW" 'librpitx installation...'
(
  cd "${BUILD_TMPDIR}"
  git clone https://github.com/F5OEO/librpitx
  cd librpitx/src
  make librpitx.a
  sudo mkdir -p /usr/local/include/librpitx
  sudo cp *.h /usr/local/include/librpitx/
  sudo install -m 0644 librpitx.a /usr/local/lib/librpitx.a
  sudo ldconfig
)
print_banner "$COLOR_YELLOW" 'librpitx installed successfully!'

print_banner "$COLOR_YELLOW" 'ft8_lib installation...'
(
  cd "${BUILD_TMPDIR}"
  git clone https://github.com/F5OEO/ft8_lib
  cd ft8_lib
  make && sudo make install
  sudo mkdir -p /usr/local/include/ft8_lib/ft8
  sudo mkdir -p /usr/local/include/ft8_lib/common
  sudo cp ft8/*.h /usr/local/include/ft8_lib/ft8/
  sudo cp common/*.h /usr/local/include/ft8_lib/common/
)
print_banner "$COLOR_YELLOW" 'ft8_lib installed successfully!'

# rpitx-ui build and installation with CMake
print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} build with CMake..."
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
sudo cmake --install build --prefix /usr
print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} built and installed successfully!"

# Update /boot/config.txt or /boot/firmware/config.txt depending on Raspbian version
echo "${INFO}: In order to run properly, rpitx-ui need to modify boot config."
echo "${INFO}: Setting the GPU frequency to 250 MHz for stable rpitx-ui operation."
if [ ! -f /boot/firmware/config.txt ]; then
  echo "${INFO}: Raspbian 11 or below detected, using /boot/config.txt"
  FILE='/boot/config.txt'
else
  echo "${INFO}: Raspbian 12 or above detected, using /boot/firmware/config.txt"
  FILE='/boot/firmware/config.txt'
fi
for LINE in 'gpu_freq=250' 'force_turbo=1'; do
  grep -qF "$LINE" "$FILE" || echo "$LINE" | sudo tee --append "$FILE" > /dev/null
done
echo "${INFO}: Boot configuration updated successfully!"

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} installation completed successfully!"

# Prompt the user to reboot the system to apply boot configuration changes
echo "${ACTION}: A reboot is required to complete the installation!"
read -p "Execute now? (y/n): " choice
# Check the user's choice
if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
  echo "${INFO}: Rebooting now..."
  sudo reboot
else
  echo "${INFO}: Reboot canceled! Please remember to reboot as soon as possible to ensure rpitx-ui works properly."
fi
