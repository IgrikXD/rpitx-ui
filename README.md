![rpitx-ui-logo](/doc/rpitx-ui-logo.png)
# About rpitx-ui
**[rpitx](https://github.com/F5OEO/rpitx)** is a general radio frequency SDR transmitter for Raspberry Pi which can work on frequencies from **5 kHz** up to **1500 MHz**. **rpitx-ui** includes changes to the _./easytest.sh_ script to make it easier to interact with the **rpitx** package via a console user interface. 

Unlike the original **[rpitx](https://github.com/F5OEO/rpitx)**, which relies on legacy **Makefiles**, **rpitx-ui** uses **CMake** as its build system. The `install.sh` script automates the entire process: installs system dependencies via `apt`, builds third-party libraries (_csdr, librpitx, ft8_lib_) from source, then configures and builds **rpitx-ui** using CMake. All compiled binaries are installed to `/usr/bin`, and resource files are installed to `/usr/share/rpitx-ui`, making it possible to run **rpitx-ui** from any directory without being tied to the cloned repository.

> [!WARNING]
> Current version of **[rpitx](https://github.com/F5OEO/rpitx)** package in the original repository has a _dvb/dvbsenco8.s_ build error! Current version of **rpitx-ui** is based on rpitx commit [cce1fe6](https://github.com/F5OEO/rpitx/commit/cce1fe6acf90d4d34ce304aed48fe80ec4ff51e7), has no build errors, and is adapted to work on **Raspberry Pi OS (_64-bit, Debian Trixie_)**.

# Installation process
Download and install **rpitx-ui** package:
```sh
git clone https://github.com/IgrikXD/rpitx-ui
cd rpitx-ui
./install.sh
```

# Uninstallation process
To completely remove **rpitx-ui** from the system, run the uninstallation script from the project directory:
```sh
cd rpitx-ui
./uninstall.sh
```
This will remove all installed binaries, shell scripts, resource files, and third-party libraries (_csdr, librpitx, ft8_lib_).

# Usage 
Plug a wire (_acts as an antenna_) on [GPIO 4](https://www.raspberrypi.com/documentation/computers/images/GPIO-Pinout-Diagram-2.png) or use [separate PCB with SMA output](https://github.com/IgrikXD/rpitx-coax-pcb). Using an expansion board will be the best option, as it will allow you to use a coaxial SMA connector to connect radio equipment and an output filter to suppress interference.

Run **rpitx-ui** application:
```sh
rpitx-ui
```

# Differences from the [original rpitx](https://github.com/F5OEO/rpitx) package
You no longer need to run the [`./easytest.sh`](./easytest.sh) command from the project directory every time. You can simply run the `rpitx-ui` command from anywhere on the system - during installation, [`./easytest.sh`](./easytest.sh) is copied to `/usr/bin/rpitx-ui` via CMake.  
![rpitx-ui-running](./doc/rpitx-ui-running.gif)

[`easytest.sh`](./easytest.sh) now has a friendlier user interface and allows you to select the specific file you want to use when transferring. The files you need should be added to the `src/resources` directory, after which you will have access to a menu for selecting a specific file when working with the "_**Spectrum**_", "_**FmRds**_", "_**NFM**_", "_**SSB**_", "_**AM**_", "_**FreeDV**_" and "_**SSTV**_" modes. [`easytest.sh`](./easytest.sh) selects files of the extension that a specific operating mode requires: for example, for the "_**FmRds**_" mode you will be asked to select only _.wav_ files from the list of all files available in the `src/resources` directory, and for the "_**SSTV**_" mode you will be asked to select file with the extension _.jpg_.  
![rpitx-ui-file-choose-process](./doc/rpitx-ui-file-choose-process.gif)

Added the ability to send a custom message when working in the "_**Pocsag**_" and "_**RTTY**_" modes. If you enter an empty message, an error message will be displayed and the transfer will not start, and you will be returned to the main menu.  
![rpitx-ui-custom-messages](./doc/rpitx-ui-custom-messages.gif)

Added the ability to specify your call sign when working in "_**Opera**_" mode. If you enter an empty call sign, an error message will be displayed and the transmission will not start, and you will be returned to the main menu.  
![rpitx-ui-custom-call-sign](./doc/rpitx-ui-custom-call-sign.gif)

Fixed a bug with displaying the "_Bye bye_" message when exiting the program - now it is displayed correctly.

## How to contact me?
- E-mail: igor.nikolaevich.96@gmail.com
- Telegram: https://t.me/igrikxd
- LinkedIn: https://www.linkedin.com/in/igor-yatsevich/