#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Re-run under bash when started by another shell (e.g. `sh install.sh`): the script relies on bash arrays, mapfile and $'...' strings
if [ -z "${BASH_VERSION:-}" ]; then
  exec bash "$0" "$@"
fi

# rpitx-ui package version
PACKAGE_VERSION='1.14'

# Installed resource directory
RESOURCE_INSTALL_DIR='/usr/share/rpitx-ui'

# Device tree directory exposing the board model and its compatible strings
DEVICE_TREE_DIR='/proc/device-tree'

# Device tree compatible strings of the SoCs whose clock, DMA and GPIO peripherals librpitx drives
SUPPORTED_SOCS=(
  'brcm,bcm2835' # Raspberry Pi 1, Zero, Zero W, Compute Module 1
  'brcm,bcm2836' # Raspberry Pi 2 Model B v1.0 / v1.1
  'brcm,bcm2837' # Raspberry Pi 2 Model B v1.2, 3, Zero 2 W, Compute Module 0 / 3 / 3+ (BCM2837, BCM2837B0, BCM2710A1)
  'brcm,bcm2711' # Raspberry Pi 4, 400, Compute Module 4 / 4S
)

# Terminal color helpers (ANSI escape sequences)
COLOR_GREEN=$'\033[32m'
COLOR_YELLOW=$'\033[33m'
COLOR_BLUE=$'\033[34m'
COLOR_RED=$'\033[31m'
COLOR_RESET=$'\033[0m'

# Status message helpers
INFO="${COLOR_YELLOW}[INFO]${COLOR_RESET}"
WARNING="${COLOR_RED}[WARNING]${COLOR_RESET}"
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

# Check that the board is built on one of SUPPORTED_SOCS: check_hardware_compatibility
# librpitx programs the clock, DMA and GPIO registers of these SoCs directly through /dev/mem.
# Other SoCs (BCM2712 in Raspberry Pi 5 / 500 / CM5, non-Broadcom boards) lay out different
# peripherals at different addresses, so rpitx-ui builds there but cannot transmit, and its
# register writes may freeze or reboot the system. The SoC is matched against the device tree
# compatible list instead of the model string, so new boards on a supported SoC pass unchanged.
check_hardware_compatibility() {
  local model='unknown model'
  local compatibles=()
  local compatible soc

  if [ -r "${DEVICE_TREE_DIR}/model" ]; then
    model=$(tr -d '\0' < "${DEVICE_TREE_DIR}/model")
  fi
  if [ -r "${DEVICE_TREE_DIR}/compatible" ]; then
    mapfile -d '' -t compatibles < "${DEVICE_TREE_DIR}/compatible"
  fi

  for compatible in "${compatibles[@]}"; do
    for soc in "${SUPPORTED_SOCS[@]}"; do
      if [ "${compatible}" = "${soc}" ]; then
        echo "${INFO} Detected board: ${model} (${soc})"
        echo "${INFO} Hardware compatibility check passed!"
        return 0
      fi
    done
  done

  print_banner "$COLOR_RED" 'Unsupported hardware detected - installation aborted!'
  if [ "${#compatibles[@]}" -eq 0 ]; then
    echo "${INFO} Unable to identify the board: ${DEVICE_TREE_DIR}/compatible is not available."
  else
    echo "${INFO} Detected board: ${model} (${compatibles[*]})"
  fi
  echo "${INFO} rpitx-ui runs only on Raspberry Pi boards built on the BCM2835, BCM2836, BCM2837 (BCM2837B0, BCM2710A1) or BCM2711 SoC."
  echo "${INFO} Other boards (Raspberry Pi 5 / 500 / CM5, Orange Pi, Radxa, etc.) do not provide the clock, DMA and GPIO hardware librpitx drives: transmission does not work and may freeze or reboot the system."
  echo "${INFO} Supported hardware list: https://github.com/IgrikXD/rpitx-ui#supported-hardware"
  echo "${INFO} No changes have been made to the system."
  echo "${INFO} To install anyway at your own risk, re-run rpitx-ui installation with the --skip-hardware-check flag."
  return 1
}

# ----------------------------------------------------------
# Command-line options
# ----------------------------------------------------------
# Default: build all project targets and their dependencies
BUILD_OPTIONAL_TARGETS=true
# Default: do not build or run tests
ENABLE_TESTING=false
# Default: abort the installation on boards without a supported Raspberry Pi SoC
SKIP_HARDWARE_CHECK=false

for arg in "$@"; do
  case "$arg" in
    --skip-optional) BUILD_OPTIONAL_TARGETS=false ;;
    --enable-testing) ENABLE_TESTING=true ;;
    --skip-hardware-check) SKIP_HARDWARE_CHECK=true ;;
    -h|--help)
      echo "Usage: $0 [--skip-optional] [--enable-testing] [--skip-hardware-check]"
      echo "  --skip-optional        Build only targets used directly by easytest.sh and available through the UI"
      echo "  --enable-testing       Build and execute tests during installation"
      echo "  --skip-hardware-check  Install without checking for a supported Raspberry Pi SoC (at your own risk)"
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

# Hardware compatibility check - runs before any change is made to the system
print_banner "$COLOR_YELLOW" 'Checking hardware compatibility...'
if [ "${SKIP_HARDWARE_CHECK}" = true ]; then
  echo "${WARNING} Hardware compatibility check skipped (--skip-hardware-check)!"
  echo "${WARNING} On unsupported hardware rpitx-ui cannot transmit, and transmission attempts may freeze or reboot the system."
elif ! check_hardware_compatibility; then
  exit 1
fi

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
if [ "${ENABLE_TESTING}" = true ]; then
  CMAKE_TESTING=ON
else
  CMAKE_TESTING=OFF
fi
cmake -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_OPTIONAL_TARGETS="${CMAKE_OPTIONAL}" \
  -DENABLE_TESTING="${CMAKE_TESTING}"
cmake --build build --parallel "$(nproc)"

# rpitx-ui tests execution (if enabled)
if [ "${ENABLE_TESTING}" = true ]; then
  print_banner "$COLOR_YELLOW" "Starting test execution..."
  # The `if ! ctest ...` form bypasses `set -e` for the ctest call so we can surface a tailored
  # diagnostic instead of the bare "Errors while running CTest" line.
  if ! ctest --test-dir build --output-on-failure --parallel "$(nproc)"; then
    print_banner "$COLOR_RED" "Test execution failed - installation aborted!"
    echo "${INFO} Review the ctest output above, fix the issues, and re-run rpitx-ui installation with the --enable-testing flag."
    echo "${INFO} No files have been installed to /usr/bin or /usr/share/rpitx-ui."
    exit 1
  fi
  print_banner "$COLOR_GREEN" "Test execution completed successfully!"
fi

sudo cmake --install build --prefix /usr
print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} built and installed successfully!"

# Update /boot/config.txt or /boot/firmware/config.txt depending on Raspberry Pi OS version
echo "${INFO} In order to run properly, rpitx-ui needs to modify boot config."
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
