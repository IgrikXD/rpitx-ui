![rpitx-ui-logo](/doc/rpitx-ui-logo.png)
## About rpitx-ui
**[rpitx](https://github.com/F5OEO/rpitx)** is a general radio frequency SDR transmitter for Raspberry Pi which can work on frequencies from **5 kHz** up to **1500 MHz**. **rpitx-ui** includes changes to the [`easytest.sh`](./easytest.sh) script to make it easier to interact with the **rpitx** package via a console user interface. 

Unlike the original **[rpitx](https://github.com/F5OEO/rpitx)**, which relies on legacy **Makefiles**, **rpitx-ui** uses **CMake** as its build system. The [`install.sh`](./install.sh) script automates the entire process: installs system dependencies via `apt`, builds third-party libraries (_csdr, librpitx, ft8_lib_) from source, then configures and builds **rpitx-ui** using CMake. All compiled binaries are installed to `/usr/bin`, and resource files are installed to `/usr/share/rpitx-ui`, making it possible to run **rpitx-ui** from any directory without being tied to the cloned repository.

> [!WARNING]
> Current version of **[rpitx](https://github.com/F5OEO/rpitx)** package in the original repository has a `dvb/dvbsenco8.s` build error! Current version of **rpitx-ui** is based on rpitx commit [cce1fe6](https://github.com/F5OEO/rpitx/commit/cce1fe6acf90d4d34ce304aed48fe80ec4ff51e7), has no build errors, and is adapted to work on **Raspberry Pi OS (_64-bit, Debian Trixie_)**.

## Project support
[![BTC: Make a donation][BTC-badge]](https://nowpayments.io/donation/wsprbeacon)&nbsp;[![PayPal: Make a donation][PayPal-badge]](https://www.paypal.com/donate/?hosted_button_id=Q8PRFPXKKSDAQ)&nbsp;[![Revolut: Make a donation][Revolut-badge]](https://revolut.me/iharygxob)

Your support helps me continue developing open-source projects like [WSPR-beacon](#WSPR-beacon) and [Easy-SDR](https://github.com/IgrikXD/Easy-SDR), while also enabling the creation of new tools that benefit the community.

## Current development progress
[![GitHub Actions: rpitx-ui build status][rpitx-ui-build-badge]](https://github.com/IgrikXD/rpitx-ui/actions/workflows/rpitx-ui-build.yml)&nbsp;![Package version](https://img.shields.io/badge/latest%20package%20version-1.3-blue.svg?longCache=true&style=for-the-badge) 

## Installation process
Download and install **rpitx-ui** package:
```sh
git clone https://github.com/IgrikXD/rpitx-ui
cd rpitx-ui
./install.sh
```

## Uninstallation process
To remove **rpitx-ui** from the system, run the uninstallation script from the project directory:
```sh
./uninstall.sh
```
This will remove all **rpitx-ui** binaries, shell scripts, and resource files. Third-party dependencies (_csdr, librpitx, ft8_lib_) are **not** removed by default, because they may be shared with other SDR projects on the system.

To also remove third-party dependencies, use the `--purge-deps` flag:
```sh
./uninstall.sh --purge-deps
```

## Usage 
Plug a wire (_acts as an antenna_) on [GPIO 4](https://www.raspberrypi.com/documentation/computers/images/GPIO-Pinout-Diagram-2.png) or use [separate PCB with SMA output](https://github.com/IgrikXD/rpitx-coax-pcb). Using an expansion board will be the best option, as it will allow you to use a coaxial SMA connector to connect radio equipment and an output filter to suppress interference.

Run **rpitx-ui** application:
```sh
rpitx-ui
```

## Differences from the [original rpitx](https://github.com/F5OEO/rpitx) package
You no longer need to run the [`./easytest.sh`](./easytest.sh) command from the project directory every time. You can simply run the `rpitx-ui` command from anywhere on the system - during installation, [`./easytest.sh`](./easytest.sh) is copied to `/usr/bin/rpitx-ui` via CMake.  
![rpitx-ui-running](./doc/rpitx-ui-running.gif)

[`easytest.sh`](./easytest.sh) now has a friendlier user interface and allows you to select the specific file you want to use when transferring. The files you need should be added to the [`src/resources`](./src/resources/) directory, after which you will have access to a menu for selecting a specific file when working with the "_**Spectrum**_", "_**FmRds**_", "_**NFM**_", "_**SSB**_", "_**AM**_", "_**FreeDV**_" and "_**SSTV**_" modes. [`easytest.sh`](./easytest.sh) selects files of the extension that a specific operating mode requires: for example, for the "_**FmRds**_" mode you will be asked to select only `.wav` files from the list of all files available in the [`src/resources`](./src/resources/) directory, and for the "_**SSTV**_" mode you will be asked to select file with the extension `.jpg`.  
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

[BTC-badge]: https://img.shields.io/badge/BTC-Make%20a%20donation-orange.svg?logo=bitcoin&style=for-the-badge
[PayPal-badge]: https://img.shields.io/badge/PayPal-Make%20a%20donation-blue.svg?logo=data:image/svg+xml;base64,PHN2ZyB2aWV3Qm94PSIwIDAgMjQgMjQiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PHBhdGggZD0iTTE5LjcxNSA2LjEzM2MuMjQ5LTEuODY2IDAtMy4xMS0uOTk5LTQuMjY2QzE3LjYzNC42MjIgMTUuNzIxIDAgMTMuMzA3IDBINi4yMzVjLS40MTggMC0uOTE2LjQ0NC0xIC44ODlMMi4zMjMgMjAuNjIyYzAgLjM1Ni4yNS44LjY2NS44aDQuMzI4bC0uMjUgMS45NTZjLS4wODQuMzU1LjE2Ni42MjIuNDk4LjYyMmgzLjY2M2MuNDE3IDAgLjgzMi0uMjY3LjkxNS0uNzExdi0uMjY3bC43NDktNC42MjJ2LS4xNzhjLjA4My0uNDQ0LjUtLjguOTE1LS44aC41YzMuNTc4IDAgNi4zMjUtMS41MSA3LjE1Ni01Ljk1NS40MTgtMS44NjcuMjUyLTMuMzc4LS43NDctNC40NDUtLjI1LS4zNTUtLjY2Ni0uNjIyLTEtLjg4OSIgZmlsbD0iIzAwOWNkZSIvPjxwYXRoIGQ9Ik0xOS43MTUgNi4xMzNjLjI0OS0xLjg2NiAwLTMuMTEtLjk5OS00LjI2NkMxNy42MzQuNjIyIDE1LjcyMSAwIDEzLjMwNyAwSDYuMjM1Yy0uNDE4IDAtLjkxNi40NDQtMSAuODg5TDIuMzIzIDIwLjYyMmMwIC4zNTYuMjUuOC42NjUuOGg0LjMyOGwxLjE2NC03LjM3OC0uMDgzLjI2N2MuMDg0LS41MzMuNS0uODg5Ljk5OC0uODg5aDIuMDhjNC4wNzkgMCA3LjI0MS0xLjc3OCA4LjI0LTYuNzU1LS4wODMtLjI2NyAwLS4zNTYgMC0uNTM0IiBmaWxsPSIjMDEyMTY5Ii8+PHBhdGggZD0iTTkuNTYzIDYuMTMzYy4wODItLjI2Ni4yNS0uNTMzLjQ5OC0uNzEuMTY2IDAgLjI1LS4wOS40MTYtLjA5aDUuNDk0Yy42NjYgMCAxLjMzLjA5IDEuODMuMTc4LjE2NiAwIC4zMzMgMCAuNDk4LjA4OS4xNjguMDg5LjMzNC4wODkuNDE4LjE3OGguMjVjLjI0OC4wODkuNDk3LjI2Ni43NDguMzU1LjI0OC0xLjg2NiAwLTMuMTEtLjk5OS00LjM1NUMxNy43MTcuNTMzIDE1LjgwNCAwIDEzLjM5IDBINi4yMzVjLS40MTggMC0uOTE2LjM1Ni0xIC44ODlMMi4zMjMgMjAuNjIyYzAgLjM1Ni4yNS44LjY2NS44aDQuMzI4bDEuMTY0LTcuMzc4IDEuMDg0LTcuOTF6IiBmaWxsPSIjMDAzMDg3Ii8+PC9zdmc+&style=for-the-badge
[Revolut-badge]: https://img.shields.io/badge/Revolut-Make%20a%20donation-black.svg?logo=data:image/svg+xml;base64,PHN2ZyByb2xlPSJpbWciIHZpZXdCb3g9IjAgMCAyNCAyNCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj48dGl0bGU+UmV2b2x1dDwvdGl0bGU+PHBhdGggZD0iTTIwLjkxMzMgNi45NTY2QzIwLjkxMzMgMy4xMjA4IDE3Ljc4OTggMCAxMy45NTAzIDBIMi40MjR2My44NjA1aDEwLjk3ODJjMS43Mzc2IDAgMy4xNzcgMS4zNjUxIDMuMjA4NyAzLjA0My4wMTYuODQtLjI5OTQgMS42MzMtLjg4NzggMi4yMzI0LS41ODg2LjU5OTgtMS4zNzUuOTMwMy0yLjIxNDQuOTMwM0g5LjIzMjJhLjI3NTYuMjc1NiAwIDAgMC0uMjc1NS4yNzUydjMuNDMxYzAgLjA1ODUuMDE4LjExNDIuMDUyLjE2MTJMMTYuMjY0NiAyNGg1LjMxMTRsLTcuMjcyNy0xMC4wOTRjMy42NjI1LS4xODM4IDYuNjEtMy4yNjEyIDYuNjEtNi45NDk0ek02Ljg5NDMgNS45MjI5SDIuNDI0VjI0aDQuNDcwNHoiLz48L3N2Zz4=&style=for-the-badge
[rpitx-ui-build-badge]: https://img.shields.io/github/actions/workflow/status/IgrikXD/rpitx-ui/rpitx-ui-build.yml?&longCache=true&style=for-the-badge&label=rpitx-ui%20build&logo=raspberry-pi