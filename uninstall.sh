#!/bin/bash

# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# rpitx-ui package version
PACKAGE_VERSION='1.10'

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
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

print_banner "$COLOR_GREEN" "Uninstalling rpitx-ui-${PACKAGE_VERSION}!"

# Remove rpitx-ui binaries from /usr/bin
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui binaries...'
BINARIES=(
  corel8 dvbrf foxhunt freedv piam pichirp pidcf77 pifm pifmrds pifsq pift8
  pimorse pinfm piopera pirfgen pirtty pissb pisstv pocsag rds_wav rpitx
  sendiq sendook spectrumpaint tune
)

for BIN in "${BINARIES[@]}"; do
  if [ -f "/usr/bin/${BIN}" ]; then
    sudo rm -f "/usr/bin/${BIN}"
    echo "${INFO} Removed /usr/bin/${BIN}"
  fi
done
echo "${INFO} Binaries removed!"

# Remove rpitx-ui shell scripts from /usr/bin
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui shell scripts...'
SCRIPTS=(
  rpitx-ui
  fm2ssb.sh ft8menu.sh rtlmenu.sh snap2spectrum.sh snapsstv.sh
  sv1afnfilter.sh testam.sh testchirp.sh testfmrds.sh testfoxhunt.sh
  testfreedv.sh testfsq.sh testlsb.sh testmorse.sh testnfm.sh
  testopera.sh testpocsag.sh testrfgen.sh testrtty.sh testspectrum.sh
  testusb.sh testsstv.sh testvfo.sh transponder.sh
)

for SCRIPT in "${SCRIPTS[@]}"; do
  if [ -f "/usr/bin/${SCRIPT}" ]; then
    sudo rm -f "/usr/bin/${SCRIPT}"
    echo "${INFO} Removed /usr/bin/${SCRIPT}"
  fi
done
echo "${INFO} Shell scripts removed!"

# Remove rpitx-ui resource files from /usr/share/rpitx-ui
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui resources...'
if [ -d /usr/share/rpitx-ui ]; then
  sudo rm -rf /usr/share/rpitx-ui
  echo "${INFO} Removed /usr/share/rpitx-ui"
fi
echo "${INFO} Resources removed!"

# Remove runtime dependencies only when --purge-deps is specified
if [ "$PURGE_DEPS" = true ]; then
  print_banner "$COLOR_YELLOW" 'Removing runtime third-party dependencies (--purge-deps)...'

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

  sudo ldconfig
  echo "${INFO} Runtime third-party dependencies removed!"
else
  echo "${INFO} Skipping runtime third-party dependencies removal (csdr)."
  echo "${INFO} Use --purge-deps to remove them."
fi

# Revert boot configuration changes
print_banner "$COLOR_YELLOW" 'Reverting boot configuration...'
if [ ! -f /boot/firmware/config.txt ]; then
  FILE='/boot/config.txt'
else
  FILE='/boot/firmware/config.txt'
fi

if [ -f "$FILE" ]; then
  BEGIN_MARKER='# BEGIN rpitx-ui related configuration'
  END_MARKER='# END rpitx-ui related configuration'
  if grep -qF "$BEGIN_MARKER" "$FILE"; then
    sudo sed -i "/^${BEGIN_MARKER}$/,/^${END_MARKER}$/d" "$FILE"
    echo "${INFO} Removed rpitx-ui block from ${FILE}"
  else
    echo "${INFO} No rpitx-ui block found in ${FILE}, nothing to revert."
  fi
fi

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} has been uninstalled successfully!"

# Prompt the user to reboot the system to apply boot configuration changes
echo "${ACTION} A reboot is recommended to apply boot configuration changes."
read -p "Reboot now? (y/n): " choice
# Check the user's choice
if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
  echo "${INFO} Rebooting now..."
  sudo reboot
else
  echo "${INFO} Reboot canceled. Please remember to reboot as soon as possible."
fi
