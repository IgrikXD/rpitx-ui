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
echo '# rpitx package configuration' >> ~/.bashrc
echo 'source '$PWD'/'$RPITX_CONFIGURATION_FILENAME'' >> ~/.bashrc
source .rpitx_profile

printf "In order to run properly, rpitx need to modify /boot/config.txt"
echo "Set GPU to 250Mhz in order to be stable"
LINE='gpu_freq=250'
FILE='/boot/config.txt'
grep -qF "$LINE" "$FILE"  || echo "$LINE" | sudo tee --append "$FILE"
#PI4
LINE='force_turbo=1'
grep -qF "$LINE" "$FILE"  || echo "$LINE" | sudo tee --append "$FILE"
echo "$(tput setaf 2)Installation completed!$(tput sgr0)"

