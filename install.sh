#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Re-run under bash when started by another shell (e.g. `sh install.sh`)
if [ -z "${BASH_VERSION:-}" ] && [ "${0##*/}" = install.sh ]; then
  exec bash "$0" "$@"
fi

# rpitx-ui package version
PACKAGE_VERSION='1.24'

# Installed resource directory
RESOURCE_INSTALL_DIR='/usr/share/rpitx-ui'

# Device tree directory exposing the board model and its compatible strings
DEVICE_TREE_DIR='/proc/device-tree'

# Device tree compatible strings of the SoCs supported by the librpitx backend
SUPPORTED_SOCS=(
  'brcm,bcm2835' # Raspberry Pi 1, Zero, Zero W, Compute Module 1
  'brcm,bcm2836' # Raspberry Pi 2 Model B v1.0 / v1.1
  'brcm,bcm2837' # Raspberry Pi 2 Model B v1.2, 3, Zero 2 W, Compute Module 0 / 3 / 3+ (BCM2837, BCM2837B0, BCM2710A1)
  'brcm,bcm2711' # Raspberry Pi 4, 400, Compute Module 4 / 4S
)

# Kernel memory statistics exposing the available memory
MEMINFO_FILE='/proc/meminfo'

# Memory budget of a single parallel build job, in MiB
BUILD_JOB_MEMORY_MIB=512

# Pinned csdr commit (to ensure reproducible builds)
CSDR_COMMIT='69bfc62'

# File recording the commit of the installed csdr
CSDR_COMMIT_FILE='/var/lib/rpitx-ui/csdr.commit'

# Boot configuration block required for stable rpitx-ui operation
BOOT_CONFIG_BEGIN_MARKER='# BEGIN rpitx-ui related configuration'
BOOT_CONFIG_END_MARKER='# END rpitx-ui related configuration'
BOOT_CONFIG_SETTINGS=(
  'gpu_freq=250' # GPU frequency fixed at 250 MHz
  'force_turbo=1' # Frequency scaling disabled
)

# Flag file of a pending reboot - created on a boot configuration change, removed by the reboot
REBOOT_REQUIRED_FLAG='/run/rpitx-ui-reboot-required'

# Terminal color helpers (ANSI escape sequences), used the same way by status tags and banners:
# cyan - information and progress, green - success, yellow - warnings, red - errors, blue - required user actions
COLOR_GREEN=$'\033[32m'
COLOR_YELLOW=$'\033[33m'
COLOR_BLUE=$'\033[34m'
COLOR_CYAN=$'\033[36m'
COLOR_RED=$'\033[31m'
COLOR_RESET=$'\033[0m'

# Status message helpers
INFO="${COLOR_CYAN}[INFO]${COLOR_RESET}"
WARNING="${COLOR_YELLOW}[WARNING]${COLOR_RESET}"
ERROR="${COLOR_RED}[ERROR]${COLOR_RESET}"
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
  echo "${INFO} Other boards (Raspberry Pi 5 / 500 / CM5, Orange Pi, Radxa, etc.) are not supported by the librpitx backend."
  echo "${INFO} Supported hardware list: https://github.com/IgrikXD/rpitx-ui#supported-hardware"
  echo "${INFO} No changes have been made to the system."
  echo "${INFO} To install anyway, re-run rpitx-ui installation with the --skip-hardware-check flag."
  return 1
}

# Limit the parallel build jobs to the CPU cores and the available memory: set_build_jobs
set_build_jobs() {
  local cpu_cores available_mib
  cpu_cores=$(nproc)
  available_mib=$(awk '$1 == "MemAvailable:" { kib = $2 } END { print int(kib / 1024) }' "${MEMINFO_FILE}")
  BUILD_JOBS=$(( available_mib / BUILD_JOB_MEMORY_MIB ))
  if [ "${BUILD_JOBS}" -gt "${cpu_cores}" ]; then
    BUILD_JOBS=${cpu_cores}
  elif [ "${BUILD_JOBS}" -lt 1 ]; then
    BUILD_JOBS=1
  fi
  echo "${INFO} Parallel build jobs: ${BUILD_JOBS} (CPU cores: ${cpu_cores}, available memory: ${available_mib} MiB)"
}

# Print each setting on standard input that differs from BOOT_CONFIG_SETTINGS and its counterpart: print_conflicting_settings
print_conflicting_settings() {
  local unconditional_lines setting

  # Conditional sections apply to some boards only and are skipped
  unconditional_lines=$(tr -d '\r' | awk '/^[ \t]*\[/ { conditional = (tolower($0) !~ /^[ \t]*\[all\]/) } !conditional')
  for setting in "${BOOT_CONFIG_SETTINGS[@]}"; do
    sed -n "s/^[[:space:]]*\(${setting%%=*}=[^[:space:]#]*\).*/\1 ${setting}/p" <<< "${unconditional_lines}" \
      | grep -v "^${setting} " || true
  done
}

# Provide only the definitions above when the script is sourced instead of executed
if [ "${BASH_SOURCE[0]}" != "$0" ]; then
  return 0
fi

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
      echo "  --skip-hardware-check  Install without checking for a supported Raspberry Pi SoC"
      exit 0
      ;;
    *) echo "${ERROR} Unknown option: $arg" >&2; exit 1 ;;
  esac
done

# ----------------------------------------------------------
# Installation script entry point
# ----------------------------------------------------------
# Exit immediately if a command exits with a non-zero status
set -e

print_banner "$COLOR_GREEN" "Installing rpitx-ui-${PACKAGE_VERSION}!"

# Hardware compatibility check - runs before any change is made to the system
print_banner "$COLOR_CYAN" 'Checking hardware compatibility...'
if [ "${SKIP_HARDWARE_CHECK}" = true ]; then
  echo "${WARNING} Hardware compatibility check skipped (--skip-hardware-check)!"
  echo "${WARNING} Correct operation of rpitx-ui is not guaranteed on unsupported hardware!"
elif ! check_hardware_compatibility; then
  exit 1
fi

# System dependency installation via package manager
print_banner "$COLOR_CYAN" 'Installing system dependencies...'
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
print_banner "$COLOR_CYAN" 'System dependencies installed successfully!'

# Parallel build jobs - limited by the available memory, so boards with little RAM do not run out of it while compiling
set_build_jobs

# csdr runtime dependency - rebuilt only when it is missing or the pinned commit differs from the installed one
if [ -x /usr/bin/csdr ] && [ "$(cat "${CSDR_COMMIT_FILE}" 2> /dev/null)" = "${CSDR_COMMIT}" ]; then
  echo "${INFO} csdr of commit ${CSDR_COMMIT} is already installed, skipping."
else
  print_banner "$COLOR_CYAN" "csdr installation, based on commit ${CSDR_COMMIT}..."
  # Build in a temporary directory to keep the source tree clean
  BUILD_TMPDIR=$(mktemp -d)
  trap 'rm -rf "${BUILD_TMPDIR}"' EXIT
  (
    cd "${BUILD_TMPDIR}"
    git clone https://github.com/F5OEO/csdr
    cd csdr
    git checkout --quiet "${CSDR_COMMIT}"
    make -j"${BUILD_JOBS}" && sudo make install PREFIX=/usr
  )
  sudo mkdir -p "$(dirname "${CSDR_COMMIT_FILE}")"
  echo "${CSDR_COMMIT}" | sudo tee "${CSDR_COMMIT_FILE}" > /dev/null
  print_banner "$COLOR_CYAN" 'csdr installed successfully!'
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
cmake --build build --parallel "${BUILD_JOBS}"

# rpitx-ui tests execution (if enabled)
if [ "${ENABLE_TESTING}" = true ]; then
  print_banner "$COLOR_CYAN" "Starting test execution..."
  # The `if ! ctest ...` form bypasses `set -e` for the ctest call so we can surface a tailored
  # diagnostic instead of the bare "Errors while running CTest" line.
  if ! ctest --test-dir build --output-on-failure --parallel "$(nproc)"; then
    print_banner "$COLOR_RED" "Test execution failed - installation aborted!"
    echo "${INFO} Review the ctest output above, fix the issues, and re-run rpitx-ui installation with the --enable-testing flag."
    echo "${INFO} No files have been installed to /usr/bin or ${RESOURCE_INSTALL_DIR}."
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
  BOOT_CONFIG_FILE='/boot/config.txt'
else
  echo "${INFO} Raspberry Pi OS 12 or above detected, using /boot/firmware/config.txt"
  BOOT_CONFIG_FILE='/boot/firmware/config.txt'
fi

# rpitx-ui block of the file - without its END marker it cannot be told from the lines after it
BOOT_CONFIG_BLOCK_RANGE="/^${BOOT_CONFIG_BEGIN_MARKER}\r\?\$/,/^${BOOT_CONFIG_END_MARKER}\r\?\$/"
INSTALLED_BLOCK=$(sed -n "${BOOT_CONFIG_BLOCK_RANGE}p" "${BOOT_CONFIG_FILE}" | tr -d '\r')
if [ -n "${INSTALLED_BLOCK}" ] && [ "$(tail -n 1 <<< "${INSTALLED_BLOCK}")" != "${BOOT_CONFIG_END_MARKER}" ]; then
  echo "${ERROR} rpitx-ui block in ${BOOT_CONFIG_FILE} is damaged (no END marker) and has not been updated." >&2
  echo "${INFO} Restore the '${BOOT_CONFIG_END_MARKER}' line or remove the block, and re-run rpitx-ui installation." >&2
  exit 1
fi

# The block stays where it is while it holds BOOT_CONFIG_SETTINGS and no setting after it differs from them
RECOMMENDED_BLOCK=$(printf '%s\n' "${BOOT_CONFIG_BEGIN_MARKER}" "${BOOT_CONFIG_SETTINGS[@]}" "${BOOT_CONFIG_END_MARKER}")
OVERRIDING_SETTINGS=$(sed -n "/^${BOOT_CONFIG_BEGIN_MARKER}\r\?\$/,\$p" "${BOOT_CONFIG_FILE}" \
  | sed "1,/^${BOOT_CONFIG_END_MARKER}\r\?\$/d" | print_conflicting_settings)
if [ "${INSTALLED_BLOCK}" = "${RECOMMENDED_BLOCK}" ] && [ -z "${OVERRIDING_SETTINGS}" ]; then
  echo "${INFO} rpitx-ui block already present in ${BOOT_CONFIG_FILE}, skipping."
else
  # Settings of the file that the block at its end overrides - the firmware applies the last assignment
  while read -r OVERRIDDEN_SETTING SETTING; do
    echo "${WARNING} ${OVERRIDDEN_SETTING} in ${BOOT_CONFIG_FILE} is overridden by ${SETTING} of the rpitx-ui block."
  done < <(print_conflicting_settings < "${BOOT_CONFIG_FILE}")

  # New file next to the present one - without the block, its last line ended, the block at the end
  NEW_BOOT_CONFIG="${BOOT_CONFIG_FILE}.rpitx-ui"
  sudo cp -p "${BOOT_CONFIG_FILE}" "${NEW_BOOT_CONFIG}"
  sudo sed -i -e "${BOOT_CONFIG_BLOCK_RANGE}d" -e "\$a\\" "${NEW_BOOT_CONFIG}"
  echo "${RECOMMENDED_BLOCK}" | sudo tee -a "${NEW_BOOT_CONFIG}" > /dev/null
  # Renamed over the present file, so an interrupted installation leaves either the old or the new file complete
  sudo mv "${NEW_BOOT_CONFIG}" "${BOOT_CONFIG_FILE}"
  # Pending reboot flag - keeps the reboot request for later installations until the system is rebooted
  sudo touch "${REBOOT_REQUIRED_FLAG}"
  echo "${INFO} Added rpitx-ui block to ${BOOT_CONFIG_FILE}"
fi

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} installation completed successfully!"

# Prompt the user to reboot the system only when a boot configuration change is waiting for it
if [ -e "${REBOOT_REQUIRED_FLAG}" ]; then
  echo "${ACTION} A reboot is required to complete the installation!"
  read -r -p "Execute now? (y/n): " choice || choice=''
  # Check the user's choice
  if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
    echo "${INFO} Rebooting now..."
    sudo reboot
  else
    echo "${INFO} Reboot canceled! Please remember to reboot as soon as possible to ensure rpitx-ui works properly."
  fi
else
  echo "${INFO} The boot configuration has not been changed, no reboot is required."
fi
