#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# Re-run under bash when started by another shell (e.g. `sh uninstall.sh`)
if [ -z "${BASH_VERSION:-}" ] && [ "${0##*/}" = uninstall.sh ]; then
  exec bash "$0" "$@"
fi

# rpitx-ui package version
PACKAGE_VERSION='1.27'

# Installed resource directory
RESOURCE_INSTALL_DIR='/usr/share/rpitx-ui'

# File recording the commit of the installed csdr
CSDR_COMMIT_FILE='/var/lib/rpitx-ui/csdr.commit'

# Markers of the boot configuration block written by install.sh
BOOT_CONFIG_BEGIN_MARKER='# BEGIN rpitx-ui related configuration'
BOOT_CONFIG_END_MARKER='# END rpitx-ui related configuration'

# Flag file of a pending reboot - created on a boot configuration change, removed by the reboot
REBOOT_REQUIRED_FLAG='/run/rpitx-ui-reboot-required'

# Terminal color helpers (ANSI escape sequences), used the same way by status tags and banners:
# cyan - information and progress, green - success, yellow - warnings, red - errors, blue - required user actions
COLOR_GREEN=$'\033[32m'
COLOR_BLUE=$'\033[34m'
COLOR_CYAN=$'\033[36m'
COLOR_RED=$'\033[31m'
COLOR_RESET=$'\033[0m'

# Status message helpers
INFO="${COLOR_CYAN}[INFO]${COLOR_RESET}"
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

# ----------------------------------------------------------
# Uninstallation script entry point
# ----------------------------------------------------------
# Exit immediately if a command exits with a non-zero status
set -e

# Parse command-line flags
PURGE_DEPS=false
for arg in "$@"; do
  case "$arg" in
    --purge-deps) PURGE_DEPS=true ;;
    -h|--help)
      echo "Usage: $0 [--purge-deps]"
      echo "  --purge-deps  Remove installed runtime third-party dependencies (csdr)"
      exit 0
      ;;
    *) echo "${ERROR} Unknown option: $arg" >&2; exit 1 ;;
  esac
done

print_banner "$COLOR_GREEN" "Uninstalling rpitx-ui-${PACKAGE_VERSION}!"

# Remove rpitx-ui binaries from /usr/bin
print_banner "$COLOR_CYAN" 'Removing rpitx-ui binaries...'
REQUIRED_BINARIES=(
  freedv piam pichirp pifmrds pimorse pinfm piopera pirfgen pirtty pispectrumpaint
  pissb pisstv pitune pocsag sendiq
)
OPTIONAL_BINARIES=(
  corel8 dvbrf foxhunt pifsq pift8 rpitx sendook
)

for BIN in "${REQUIRED_BINARIES[@]}" "${OPTIONAL_BINARIES[@]}"; do
  if [ -f "/usr/bin/${BIN}" ]; then
    sudo rm -f "/usr/bin/${BIN}"
    echo "${INFO} Removed /usr/bin/${BIN}"
  fi
done
echo "${INFO} Binaries removed!"

# Remove rpitx-ui shell scripts from /usr/bin
print_banner "$COLOR_CYAN" 'Removing rpitx-ui shell scripts...'
SCRIPTS=(
  rpitx-ui
  fm2ssb.sh ft8menu.sh rtlmenu.sh snap2spectrum.sh testam.sh testchirp.sh
  testfmrds.sh testfoxhunt.sh testfreedv.sh testfsq.sh testmorse.sh
  testnfm.sh testopera.sh testpocsag.sh testrfgen.sh testrtty.sh
  testspectrum.sh testssb.sh testsstv.sh testtune.sh transponder.sh
)

for SCRIPT in "${SCRIPTS[@]}"; do
  if [ -f "/usr/bin/${SCRIPT}" ]; then
    sudo rm -f "/usr/bin/${SCRIPT}"
    echo "${INFO} Removed /usr/bin/${SCRIPT}"
  fi
done
echo "${INFO} Shell scripts removed!"

# Remove rpitx-ui resource files from /usr/share/rpitx-ui
print_banner "$COLOR_CYAN" 'Removing rpitx-ui resources...'
if [ -d "${RESOURCE_INSTALL_DIR}" ]; then
  sudo rm -rf "${RESOURCE_INSTALL_DIR}"
  echo "${INFO} Resources removed from ${RESOURCE_INSTALL_DIR}"
fi

# Remove runtime dependencies only when --purge-deps is specified
if [ "$PURGE_DEPS" = true ]; then
  print_banner "$COLOR_CYAN" 'Removing runtime third-party dependencies (--purge-deps)...'

  # Remove csdr
  echo "${INFO} Removing csdr..."
  for BIN in csdr csdr-fm nmux; do
    if [ -f "/usr/bin/${BIN}" ]; then
      sudo rm -f "/usr/bin/${BIN}"
      echo "${INFO} Removed /usr/bin/${BIN}"
    fi
  done

  for LIB in /usr/lib/libcsdr.so*; do
    if [ -f "${LIB}" ]; then
      sudo rm -f "${LIB}"
      echo "${INFO} Removed ${LIB}"
    fi
  done

  if [ -f "${CSDR_COMMIT_FILE}" ]; then
    sudo rm -f "${CSDR_COMMIT_FILE}"
    sudo rmdir --ignore-fail-on-non-empty "$(dirname "${CSDR_COMMIT_FILE}")"
    echo "${INFO} Removed ${CSDR_COMMIT_FILE}"
  fi

  sudo ldconfig
  echo "${INFO} Runtime third-party dependencies removed!"
else
  echo "${INFO} Skipping runtime third-party dependencies removal (csdr)."
  echo "${INFO} Use --purge-deps to remove them."
fi

# Revert boot configuration changes
print_banner "$COLOR_CYAN" 'Reverting boot configuration...'
if [ ! -f /boot/firmware/config.txt ]; then
  BOOT_CONFIG_FILE='/boot/config.txt'
else
  BOOT_CONFIG_FILE='/boot/firmware/config.txt'
fi

# rpitx-ui block of the file - without its END marker it cannot be told from the lines after it
BOOT_CONFIG_BLOCK_RANGE="/^${BOOT_CONFIG_BEGIN_MARKER}\r\?\$/,/^${BOOT_CONFIG_END_MARKER}\r\?\$/"
INSTALLED_BLOCK=$(sed -n "${BOOT_CONFIG_BLOCK_RANGE}p" "${BOOT_CONFIG_FILE}" | tr -d '\r')
if [ -z "${INSTALLED_BLOCK}" ]; then
  echo "${INFO} No rpitx-ui block found in ${BOOT_CONFIG_FILE}, nothing to revert."
elif [ "$(tail -n 1 <<< "${INSTALLED_BLOCK}")" != "${BOOT_CONFIG_END_MARKER}" ]; then
  echo "${ERROR} rpitx-ui block in ${BOOT_CONFIG_FILE} is damaged (no END marker) and has not been removed." >&2
  echo "${INFO} Restore the '${BOOT_CONFIG_END_MARKER}' line or remove the block, and re-run rpitx-ui uninstallation." >&2
  exit 1
else
  sudo sed -i "${BOOT_CONFIG_BLOCK_RANGE}d" "${BOOT_CONFIG_FILE}"
  # Pending reboot flag - keeps the reboot request for later runs until the system is rebooted
  sudo touch "${REBOOT_REQUIRED_FLAG}"
  echo "${INFO} Removed rpitx-ui block from ${BOOT_CONFIG_FILE}"
fi

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} has been uninstalled successfully!"

# Prompt the user to reboot the system only when a boot configuration change is waiting for it
if [ -e "${REBOOT_REQUIRED_FLAG}" ]; then
  echo "${ACTION} A reboot is recommended to apply boot configuration changes."
  read -r -p "Reboot now? (y/n): " choice || choice=''
  # Check the user's choice
  if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
    echo "${INFO} Rebooting now..."
    sudo reboot
  else
    echo "${INFO} Reboot canceled. Please remember to reboot as soon as possible."
  fi
else
  echo "${INFO} The boot configuration has not been changed, no reboot is required."
fi
