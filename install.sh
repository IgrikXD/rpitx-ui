#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# rpitx-ui package version
PACKAGE_VERSION='1.11'

# Installed resource directory
RESOURCE_INSTALL_DIR='/usr/share/rpitx-ui'

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
# Command-line options
# ----------------------------------------------------------
# Default: build all project targets and their dependencies
BUILD_OPTIONAL_TARGETS=true

for arg in "$@"; do
  case "$arg" in
    --skip-optional) BUILD_OPTIONAL_TARGETS=false ;;
    -h|--help)
      echo "Usage: $0 [--skip-optional]"
      echo "  --skip-optional  Build only targets used directly by easytest.sh and available through the UI"
      exit 0
      ;;
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

# ----------------------------------------------------------
# Installation script entry point
# ----------------------------------------------------------
# Exit immediately if a command exits with a non-zero status
set -e

print_banner "$COLOR_GREEN" "Installing rpitx-ui-${PACKAGE_VERSION}!"

# System dependency installation via package manager
print_banner "$COLOR_YELLOW" 'Installing system dependencies...'
sudo apt-get update
sudo apt-get install -y \
  buffer \
  cmake \
  imagemagick \
  libcli11-dev \
  libfftw3-dev \
  libsndfile1-dev \
  libsoxr-dev \
  pkg-config \
  rtl-sdr
print_banner "$COLOR_YELLOW" 'System dependencies installed successfully!'

# Build the csdr runtime dependency in a temporary directory to keep the source tree clean
BUILD_TMPDIR=$(mktemp -d)
trap 'rm -rf "${BUILD_TMPDIR}"' EXIT

# Pinned dependency commits (to ensure reproducible builds)
CSDR_COMMIT='69bfc62'

print_banner "$COLOR_YELLOW" "csdr installation, based on commit ${CSDR_COMMIT}..."
(
  cd "${BUILD_TMPDIR}"
  git clone https://github.com/F5OEO/csdr
  cd csdr
  git checkout --quiet "${CSDR_COMMIT}"
  make -j"$(nproc)" && sudo make install PREFIX=/usr
)
print_banner "$COLOR_YELLOW" 'csdr installed successfully!'

if [ -d "${RESOURCE_INSTALL_DIR}" ]; then
  sudo find "${RESOURCE_INSTALL_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
  echo "${INFO} Cleared existing resources from ${RESOURCE_INSTALL_DIR}"
fi

# rpitx-ui build and installation with CMake
print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} build with CMake..."
if [ "${BUILD_OPTIONAL_TARGETS}" = true ]; then
  CMAKE_OPTIONAL=ON
else
  CMAKE_OPTIONAL=OFF
fi
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_OPTIONAL_TARGETS="${CMAKE_OPTIONAL}"
cmake --build build --parallel "$(nproc)"
sudo cmake --install build --prefix /usr
print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} built and installed successfully!"

# Update /boot/config.txt or /boot/firmware/config.txt depending on Raspberry Pi OS version
echo "${INFO} In order to run properly, rpitx-ui need to modify boot config."
echo "${INFO} Setting the GPU frequency to 250 MHz for stable rpitx-ui operation."
if [ ! -f /boot/firmware/config.txt ]; then
  echo "${INFO} Raspberry Pi OS 11 or below detected, using /boot/config.txt"
  FILE='/boot/config.txt'
else
  echo "${INFO} Raspberry Pi OS 12 or above detected, using /boot/firmware/config.txt"
  FILE='/boot/firmware/config.txt'
fi
BEGIN_MARKER='# BEGIN rpitx-ui related configuration'
END_MARKER='# END rpitx-ui related configuration'
if grep -qF "$BEGIN_MARKER" "$FILE"; then
  echo "${INFO} rpitx-ui block already present in ${FILE}, skipping."
else
  printf '%s\n' "$BEGIN_MARKER" 'gpu_freq=250' 'force_turbo=1' "$END_MARKER" | sudo tee --append "$FILE" > /dev/null
fi
echo "${INFO} Boot configuration updated successfully!"

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} installation completed successfully!"

# Prompt the user to reboot the system to apply boot configuration changes
echo "${ACTION} A reboot is required to complete the installation!"
read -p "Execute now? (y/n): " choice
# Check the user's choice
if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
  echo "${INFO} Rebooting now..."
  sudo reboot
else
  echo "${INFO} Reboot canceled! Please remember to reboot as soon as possible to ensure rpitx-ui works properly."
fi
