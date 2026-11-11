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
PACKAGE_VERSION='1.24'

# Installed resource directory
RESOURCE_INSTALL_DIR='/usr/share/rpitx-ui'

# Manifest of the installed csdr
CSDR_MANIFEST='/var/lib/rpitx-ui/csdr.sha256'

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

# Remove the rpitx-ui blocks and unpaired markers from a boot configuration file: remove_boot_config <file>
remove_boot_config() {
  local boot_config=$1
  local updated_config

  # Drop the markers and the lines between paired ones except filter lines, keep the lines after an unpaired BEGIN marker
  updated_config=$(mktemp)
  awk -v begin="${BOOT_CONFIG_BEGIN_MARKER}" -v end="${BOOT_CONFIG_END_MARKER}" '
    { line = $0; sub(/\r$/, "", line) }
    line == begin { printf "%s", pending; pending = filters = ""; in_block = 1; next }
    line == end { printf "%s", filters; pending = filters = ""; in_block = 0; next }
    in_block && line ~ /^\[/ { filters = filters $0 "\n" }
    in_block { pending = pending $0 "\n"; next }
    { print }
    END { printf "%s", pending }
  ' < "${boot_config}" > "${updated_config}" || return 1
  if [ "$(< "${boot_config}")" = "$(< "${updated_config}")" ]; then
    BOOT_CONFIG_CHANGED=false
    echo "${INFO} No rpitx-ui block found in ${boot_config}, nothing to revert."
  else
    sudo cp "${updated_config}" "${boot_config}" || return 1
    BOOT_CONFIG_CHANGED=true
    echo "${INFO} Removed rpitx-ui block from ${boot_config}"
  fi
  rm -f "${updated_config}"
}

# Provide only the definitions above when the script is sourced instead of executed
if [ "${BASH_SOURCE[0]}" != "$0" ]; then
  return 0
fi

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
  corel8 dvbrf foxhunt pidcf77 pifsq pift8 rpitx sendook
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
  fm2ssb.sh ft8menu.sh rtlmenu.sh snap2spectrum.sh snapsstv.sh
  sv1afnfilter.sh testam.sh testchirp.sh testfmrds.sh testfoxhunt.sh
  testfreedv.sh testfsq.sh testmorse.sh testnfm.sh
  testopera.sh testpocsag.sh testrfgen.sh testrtty.sh testspectrum.sh
  testssb.sh testsstv.sh testtune.sh transponder.sh
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

  if [ -f "${CSDR_MANIFEST}" ]; then
    sudo rm -f "${CSDR_MANIFEST}"
    sudo rmdir --ignore-fail-on-non-empty "$(dirname "${CSDR_MANIFEST}")"
    echo "${INFO} Removed ${CSDR_MANIFEST}"
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

if [ -f "${BOOT_CONFIG_FILE}" ]; then
  remove_boot_config "${BOOT_CONFIG_FILE}"
  # Pending reboot flag - keeps the reboot request for later runs until the system is rebooted
  if [ "${BOOT_CONFIG_CHANGED}" = true ]; then
    sudo touch "${REBOOT_REQUIRED_FLAG}"
  fi
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
