#!/bin/bash

echo "$(tput setaf 3)Installing rpitx!$(tput sgr0)"

sudo apt update
sudo apt install -y libsndfile1-dev git
sudo apt install -y imagemagick libfftw3-dev
#For rtl-sdr use
sudo apt install -y rtl-sdr buffer
# We use CSDR as a dsp for analogs modes thanks to HA7ILM
git clone https://github.com/F5OEO/csdr
cd csdr || exit
make && sudo make install
cd ../ || exit

cd src || exit
git clone https://github.com/F5OEO/librpitx
cd librpitx/src || exit
make && sudo make install
cd ../../ || exit

cd pift8
git clone https://github.com/F5OEO/ft8_lib
cd ft8_lib
make && sudo make install
cd ../
make
cd ../

make
sudo make install
cd .. || exit

RPITX_RESOURCES_LOCATION=$PWD/src/resources
RPITX_CONFIGURATION_FILENAME=.rpitx_profile
echo 'export RPITX_RESOURCES_LOCATION='$RPITX_RESOURCES_LOCATION'' > $RPITX_CONFIGURATION_FILENAME
echo '# rpitx-ui package configuration' >> ~/.bashrc
echo 'source '$PWD'/'$RPITX_CONFIGURATION_FILENAME'' >> ~/.bashrc

echo "$(tput setaf 3)[INFO]$(tput sgr0): In order to run properly, rpitx need to modify /boot/config.txt"
echo "$(tput setaf 3)[INFO]$(tput sgr0): Setting the GPU frequency to 250 MHz for stable rpitx operation."
LINE='gpu_freq=250'
FILE='/boot/config.txt'
grep -qF "$LINE" "$FILE"  || echo "$LINE" | sudo tee --append "$FILE"
#PI4
LINE='force_turbo=1'
grep -qF "$LINE" "$FILE"  || echo "$LINE" | sudo tee --append "$FILE"
echo "$(tput setaf 2)Installation completed!"

echo "$(tput setaf 3)[ACTION REQUIRED]$(tput sgr0): A reboot is required to complete the installation!"
read -p "Execute now? (y/n): " choice

# Check the user's choice
if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
  echo "$(tput setaf 3)[INFO]$(tput sgr0) Rebooting now..."
  sudo reboot  # You may need to run this with sudo privileges
else
  echo "$(tput setaf 3)[INFO]$(tput sgr0) Reboot canceled."
fi

