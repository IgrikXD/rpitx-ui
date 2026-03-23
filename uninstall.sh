#!/bin/bash

# rpitx-ui package version
PACKAGE_VERSION='1.3'

# Terminal color helpers
COLOR_GREEN=$(tput setaf 2)
COLOR_YELLOW=$(tput setaf 3)
COLOR_BLUE=$(tput setaf 4)
COLOR_RESET=$(tput sgr0)

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

print_banner "$COLOR_GREEN" "Uninstalling rpitx-ui-${PACKAGE_VERSION}!"

# Remove rpitx-ui binaries from /usr/bin
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui binaries...'
BINARIES=(
  corel8 foxhunt freedv morse pichirp pifsq pifmrds pift8
  piopera pirtty pisstv pocsag rpitx sendiq sendook spectrumpaint tune
  dvbrf piam pidcf77 pifm pissb rds_wav testssb
)

for BIN in "${BINARIES[@]}"; do
  if [ -f "/usr/bin/${BIN}" ]; then
    sudo rm -f "/usr/bin/${BIN}"
    echo "${INFO}: Removed /usr/bin/${BIN}"
  fi
done
echo "${INFO}: Binaries removed!"

# Remove rpitx-ui shell scripts from /usr/bin
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui shell scripts...'
SCRIPTS=(
  rpitx-ui
  fm2ssb.sh ft8menu.sh rtlmenu.sh snap2spectrum.sh snapsstv.sh
  sv1afnfilter.sh testam.sh testchirp.sh testfmrds.sh testfoxhunt.sh
  testfreedv.sh testfsq.sh testnfm.sh testopera.sh testpocsag.sh
  testrtty.sh testspectrum.sh testssb.sh testsstv.sh testvfo.sh
  transponder.sh
)

for SCRIPT in "${SCRIPTS[@]}"; do
  if [ -f "/usr/bin/${SCRIPT}" ]; then
    sudo rm -f "/usr/bin/${SCRIPT}"
    echo "${INFO}: Removed /usr/bin/${SCRIPT}"
  fi
done
echo "${INFO}: Shell scripts removed!"

# Remove rpitx-ui resource files from /usr/share/rpitx-ui
print_banner "$COLOR_YELLOW" 'Removing rpitx-ui resources...'
if [ -d /usr/share/rpitx-ui ]; then
  sudo rm -rf /usr/share/rpitx-ui
  echo "${INFO}: Removed /usr/share/rpitx-ui"
fi
echo "${INFO}: Resources removed!"

# Remove librpitx
print_banner "$COLOR_YELLOW" 'Removing librpitx...'
if [ -d /usr/local/include/librpitx ]; then
  sudo rm -rf /usr/local/include/librpitx
  echo "${INFO}: Removed /usr/local/include/librpitx/"
fi

if [ -f /usr/local/lib/librpitx.a ]; then
  sudo rm -f /usr/local/lib/librpitx.a
  echo "${INFO}: Removed /usr/local/lib/librpitx.a"
  sudo ldconfig
fi
echo "${INFO}: librpitx removed!"

# Remove ft8_lib
print_banner "$COLOR_YELLOW" 'Removing ft8_lib...'
if [ -f /usr/lib/libft8.a ]; then
  sudo rm -f /usr/lib/libft8.a
  echo "${INFO}: Removed /usr/lib/libft8.a"
fi

if [ -d /usr/local/include/ft8_lib ]; then
  sudo rm -rf /usr/local/include/ft8_lib
  echo "${INFO}: Removed /usr/local/include/ft8_lib/"
fi
sudo ldconfig
echo "${INFO}: ft8_lib removed!"

# Remove csdr
print_banner "$COLOR_YELLOW" 'Removing csdr...'
for BIN in csdr csdr-fm nmux; do
  if [ -f "/usr/bin/${BIN}" ]; then
    sudo rm -f "/usr/bin/${BIN}"
    echo "${INFO}: Removed /usr/bin/${BIN}"
  fi
done

for LIB in /usr/lib/libcsdr.so*; do
  if [ -f "${LIB}" ]; then
    sudo rm -f "${LIB}"
    echo "${INFO}: Removed ${LIB}"
  fi
done
sudo ldconfig
echo "${INFO}: csdr removed!"

# Revert boot configuration changes
print_banner "$COLOR_YELLOW" 'Reverting boot configuration...'
if [ ! -f /boot/firmware/config.txt ]; then
  FILE='/boot/config.txt'
else
  FILE='/boot/firmware/config.txt'
fi

if [ -f "$FILE" ]; then
  for LINE in 'gpu_freq=250' 'force_turbo=1'; do
    if grep -qF "$LINE" "$FILE"; then
      sudo sed -i "/^${LINE}$/d" "$FILE"
      echo "${INFO}: Removed '${LINE}' from ${FILE}"
    fi
  done
fi
echo "${INFO}: Boot configuration reverted!"

print_banner "$COLOR_GREEN" "rpitx-ui-${PACKAGE_VERSION} has been uninstalled successfully!"

# Prompt the user to reboot the system to apply boot configuration changes
echo "${ACTION}: A reboot is recommended to apply boot configuration changes."
read -p "Reboot now? (y/n): " choice
# Check the user's choice
if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
  echo "${INFO}: Rebooting now..."
  sudo reboot
else
  echo "${INFO}: Reboot canceled. Please remember to reboot as soon as possible."
fi
