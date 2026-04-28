![rpitx-ui-logo](/doc/rpitx-ui-logo.png)
## About rpitx-ui
**[rpitx]** is a general radio frequency SDR transmitter for Raspberry Pi which can work on frequencies from **5 kHz** up to **1500 MHz**. 

**rpitx-ui** builds upon this project by providing a [convenient console user interface](#user-interface-changes) to simplify interaction with the **[rpitx]** package. Furthermore, unlike the original **[rpitx]**, which relies on legacy **Makefiles**, **rpitx-ui** uses **CMake** as its modern build system. The [`install.sh`](./install.sh) script automates the entire process: installs system dependencies via `apt`, installs the runtime `csdr` tools from source, then configures and builds **rpitx-ui**. CMake fetches and builds `librpitx` and, when optional targets are enabled, `ft8_lib` under the local build tree and links them into the relevant binaries. All compiled binaries are installed to `/usr/bin`, and resource files are installed to `/usr/share/rpitx-ui`, making it possible to run **rpitx-ui** from any directory without being tied to the cloned repository.

> [!WARNING]
> The current version of the **[rpitx]** package in the original repository has a `dvb/dvbsenco8.s` build error. The current version of **rpitx-ui** is based on rpitx commit [cce1fe6](https://github.com/F5OEO/rpitx/commit/cce1fe6acf90d4d34ce304aed48fe80ec4ff51e7), has no build errors, and is adapted to work on **Raspberry Pi OS (_64-bit, Debian Trixie_)**.

## Project support
[![BTC: Make a donation][BTC-badge]](https://nowpayments.io/donation/wsprbeacon)&nbsp;[![PayPal: Make a donation][PayPal-badge]](https://www.paypal.com/donate/?hosted_button_id=Q8PRFPXKKSDAQ)&nbsp;[![Revolut: Make a donation][Revolut-badge]](https://revolut.me/iharygxob)

Your support helps me continue developing open-source projects like [WSPR-beacon](https://github.com/IgrikXD/WSPR-beacon) and [Easy-SDR](https://github.com/IgrikXD/Easy-SDR), while also enabling the creation of new tools that benefit the community.

## Current development progress
[![GitHub Actions: rpitx-ui build status][rpitx-ui-build-badge]](https://github.com/IgrikXD/rpitx-ui/actions/workflows/rpitx-ui-build.yml)&nbsp;![Package version](https://img.shields.io/badge/latest%20package%20version-1.11-blue.svg?longCache=true&style=for-the-badge)

## Installation process
Clone the **rpitx-ui** repository:
```sh
git clone https://github.com/IgrikXD/rpitx-ui
cd rpitx-ui
```
Optionally, you can add any resource files you need to the [`src/resources`](./src/resources/) directory before installation. They will be copied to the system resource directory `/usr/share/rpitx-ui` during the installation process.

Install the **rpitx-ui** package:
```sh
./install.sh
```

To build only the core targets used directly by the `rpitx-ui` interface (_skipping optional binaries and the ft8_lib dependency_), use the `--skip-optional` flag:
```sh
./install.sh --skip-optional
```

To add new files for transmission after installation, place them directly into `/usr/share/rpitx-ui`. You can override the default resource directory path by setting the `RPITX_RESOURCES_LOCATION` environment variable.

## Uninstallation process
To remove **rpitx-ui** from the system, run the uninstallation script from the project directory:
```sh
./uninstall.sh
```
This will remove all **rpitx-ui** binaries, shell scripts, and resource files. The `csdr` runtime dependency is **not** removed by default, because it may be shared with other SDR projects on the system.

To also remove the `csdr` runtime dependency, use the `--purge-deps` flag:
```sh
./uninstall.sh --purge-deps
```

## Usage
Plug a wire (_acts as an antenna_) on [GPIO 4](https://www.raspberrypi.com/documentation/computers/images/GPIO-Pinout-Diagram-2.png) or use [a separate PCB with SMA output](https://github.com/IgrikXD/rpitx-coax-pcb). Using an expansion board is the best option, as it allows you to use a coaxial SMA connector to connect radio equipment and an output filter to suppress interference.

Run the **rpitx-ui** application:
```sh
rpitx-ui
```

## Differences from the original [rpitx] package

### User interface changes
You no longer need to run the [`./easytest.sh`](./easytest.sh) command from the project directory every time. You can simply run the `rpitx-ui` command from anywhere on the system - during installation, [`./easytest.sh`](./easytest.sh) is copied to `/usr/bin/rpitx-ui` via CMake.  
![rpitx-ui-running](./doc/rpitx-ui-running.gif)

[`easytest.sh`](./easytest.sh) now has a friendlier user interface and allows you to select the specific file you want to use when transmitting. You will have access to a menu for selecting a specific file when working with the "_**Spectrum**_", "_**FmRds**_", "_**NFM**_", "_**USB**_", "_**LSB**_", "_**AM**_", "_**FreeDV**_" and "_**SSTV**_" modes. [`easytest.sh`](./easytest.sh) selects files with the extension required for a specific operating mode: for example, for the "_**FmRds**_" mode you will be asked to select only `.wav` files, and for the "_**SSTV**_" mode you will be asked to select a file with the `.jpg` extension.  
![rpitx-ui-file-choose-process](./doc/rpitx-ui-file-choose-process.gif)

Added the ability to send a custom message when working in the "_**Pocsag**_", "_**RTTY**_" and "_**CW**_" modes. If you enter an empty message, an error message will be displayed and the transfer will not start, and you will be returned to the main menu.  
![rpitx-ui-custom-messages](./doc/rpitx-ui-custom-messages.gif)

Added the ability to specify your call sign when working in "_**Opera**_" mode. If you enter an empty call sign, an error message will be displayed and the transmission will not start, and you will be returned to the main menu.  
![rpitx-ui-custom-call-sign](./doc/rpitx-ui-custom-call-sign.gif)

Added "_**CW**_" mode for Morse code transmission. You can enter a custom message and specify the transmission speed in words per minute (_WPM_). If you enter an empty message or WPM value, an error message will be displayed and the transmission will not start.  
![rpitx-ui-cw-mode](./doc/rpitx-ui-cw-mode.gif)

Added "_**RFgen**_" mode for wideband RF signal generation. You can select one of three generator types: "_**Noise**_" (_uniform pseudo-random noise across the bandwidth_), "_**Sweep**_" (_fast sawtooth sweep across the bandwidth_), or "_**Multitone**_" (_random fast-hopping across equidistant tones_). After selecting the generator type, you will be asked to enter the bandwidth in Hz. If "_**Multitone**_" is selected, you will also be asked to specify the number of tones. If you enter an empty or invalid value for bandwidth or tone count, an error message will be displayed and the transmission will not start.  
![rpitx-ui-rfgen-mode](./doc/rpitx-ui-rfgen-mode.gif)

Added NFM deviation-mode selection for the "_**NFM**_" transmitter. After choosing a `.wav` file, you can now select either "_**Wide**_" (_+-5 kHz deviation for 25 kHz amateur VHF/UHF channels_) or "_**Narrow**_" (_+-2.5 kHz deviation for 12.5 kHz PMR/DMR-style channels_) before transmission starts.
![rpitx-ui-nfm-mode](./doc/rpitx-ui-nfm-mode.gif)

Added RDS parameter setup for the "_**FmRds**_" transmitter. After choosing a `.wav` file, you can now enter the RDS Programme Identification (_PI_) code, Programme Service (_PS_) name, RadioText (_RT_), and select either 50 us or 75 us FM pre-emphasis before transmission starts. Empty or invalid PI / PS / RT values are rejected and the transmission is not started.
![rpitx-ui-fmrds-mode](./doc/rpitx-ui-fmrds-mode.gif)

Fixed a bug affecting the display of the "_Bye bye_" message when exiting the program; it is now shown correctly.

### Changes to core functionality
SSB modulation has been completely rewritten from scratch. In the original **[rpitx]**, the SSB option in `easytest.sh` suffered from a significant delay between initiating transmission and the actual start of the RF output, making it impractical for real use. This has been replaced with a standalone [`pissb`](./src/pissb/) binary that opens an audio file through `libsndfile`, downmixes/resamples it to 48 kHz, runs the DSP chain internally, and drives `librpitx::iqdmasync` directly: a 300-3000 Hz bandpass filter (_biquad HPF + LPF_), a 255-tap Blackman-windowed Hilbert transform for analytic signal generation, and an asymmetric attack/decay AGC. The former single "_**SSB**_" menu entry has been split into separate "_**USB**_" (_Upper Side Band_) and "_**LSB**_" (_Lower Side Band_) options in [`easytest.sh`](./easytest.sh), which allows you direct sideband selection for transmission.

The Morse code transmitter has been rewritten from scratch and renamed from `morse` to [`pimorse`](./src/pimorse/) to align with the project's naming convention (_pinfm, pissb, pirtty, etc._). The original implementation used C headers, `printf` for output, `atof` for argument parsing (_which silently returns 0 on invalid input_), and a fixed-size `char cw[23]` buffer for CW conversion that could overflow on longer patterns. The new version uses modern C++ features: `std::from_chars`-based numeric argument parsing via the shared [`cli`](./src/utils/cli/) helpers (_locale-independent, allocation-free, reports failure via `std::optional` instead of exceptions_), `std::optional` for safe Morse table lookup instead of raw `NULL` pointers, `std::string` for dynamically sized CW buffers, and named `constexpr` constants instead of magic numbers. The dit timing has been corrected to the canonical PARIS relation `symbolRate = WPM / 1.2` (_one dit = exactly `1200/WPM` ms_), replacing the earlier `WPM / 1.25` approximation that ran ~4 % slow. The Morse encoding logic (_ITU lookup table and CW OOK binary conversion_) has been extracted into reusable [`morse_encoder`](./src/pimorse/morse_encoder.h) utilities, and the table now covers A-Z, 0-9, space, and the 15 standard ITU punctuation characters (_`. , ? / = + - ( ) : ; ' " @ !`_) so typical beacon and CQ messages transmit without silently skipping characters.

The [`pichirp`](./src/pichirp/) chirp transmitter has been rewritten to align with the modern C++20 conventions used by [`pissb`](./src/pissb/) and [`pimorse`](./src/pimorse/). The original implementation used `atof` for argument parsing (_which silently returns 0 on invalid input_), ran non-async-signal-safe `fprintf` from a handler installed for all 64 signals, mutated a plain `bool running` from that handler (_data race_), accepted bandwidths above the Nyquist limit without diagnosis, and contained dead code such as an unused `SimpleTestDMA()` function. The new version uses `std::atomic<bool>` for the stop flag with the handler restricted to `SIGTERM` and `SIGINT`, validates all three positional arguments with `std::isfinite` and strict-positivity checks, adds an explicit Nyquist check, and switches phase accumulation to `double` with `std::numbers::pi_v<double>` for full precision on long sweeps. Argument parsing, help-flag handling, and error reporting are delegated to the shared [`cli`](./src/utils/cli/) helpers from the `rpitx_cli_utils` library.

The AM transmitter has been rewritten from scratch as a standalone [`piam`](./src/piam/) binary, and the "_**AM**_" menu entry is no longer labelled "_Poor quality_" in [`easytest.sh`](./easytest.sh). The original path piped audio through an external `csdr` pipeline (_`csdr dsb_fc` generating AM IQ samples_) into `rpitx` in `IQFLOAT` mode; while `csdr` itself produced a valid AM waveform, the Pi has no amplitude-capable DAC - the only on-chip amplitude control is a coarse 3-bit GPIO pad-drive quantiser (_eight discrete drive-strength levels_) - so the `IQFLOAT` pipeline could not reproduce the AM envelope cleanly, and the on-air spectrum lacked a clean constant carrier and exhibited smeared sideband energy more consistent with FM than with textbook AM. The new version drives `librpitx::amdmasync` directly - a purpose-built AM path that maps the audio envelope straight onto the pad-drive quantiser while keeping the carrier frequency fixed - opens the input through `libsndfile`, downmixes/resamples it to 48 kHz, and runs it through a dedicated [`AmProcessor`](./src/piam/am_processor.h) chain: 30 Hz HPF -> 4500 Hz LPF -> scalar AGC -> DSB-FC envelope formation `s = 0.5 * (1 + m * a)` with `m = 0.9`, clamped to `[0, 1]`. The shared `rpitx_dsp_utils` library provides the scalar-sample overload on `Agc` for this use case.

The wideband RF generator [`pirfgen`](./src/pirfgen/) is an entirely new binary with no equivalent in the original **[rpitx]**. It implements three generator modes through a polymorphic generator architecture: an `RfGenerator` abstract base class, and three concrete implementations - `NoiseGenerator` (_uniform pseudo-random frequency offsets with sample-and-hold band-limiting_), `SweepGenerator` (_deterministic linear sawtooth ramp_), and `MultitoneGenerator` (_FHSS-style random hopping across a pre-computed equidistant tone comb_). The `RfGenProcessor` facade owns the active generator via `std::unique_ptr` and is constructed through a factory that switches on the `RfGenMode` scoped enum. CLI parsing uses `std::from_chars` for locale-independent, allocation-free numeric conversion and `std::span` for argv slicing. All user-supplied parameters are validated before transmission starts.

> [!CAUTION]
> The "_**RFgen**_" mode is intended **exclusively for laboratory use** (_shielded-room interference testing, receiver sensitivity/selectivity/blocking evaluation, RF front-end and filter characterization, radio protocol resilience evaluation_) and research purposes only. Using this mode to transmit over the air may violate radio spectrum regulations and result in serious legal consequences. The author assumes no responsibility for any misuse of this functionality.

The narrow-band FM transmitter has been rewritten from the legacy `csdr`/`rpitx` shell pipeline into a standalone [`pinfm`](./src/pinfm/) binary. The original [`testnfm.sh`](./scripts/testnfm.sh) path converted PCM audio through several external `csdr` stages and fed `rpitx` in raw RF mode, with no built-in deviation presets, AGC, or explicit audio-bandwidth guard. The new version opens the input through `libsndfile`, downmixes/resamples it to 48 kHz, and drives `librpitx::ngfmdmasync` directly with per-sample frequency-deviation values. Its dedicated [`NfmProcessor`](./src/pinfm/nfm_processor.h) chain applies a 30 Hz HPF, a cascaded 4th-order 3000 Hz Butterworth LPF, scalar AGC, and a hard clamp before scaling to the selected peak deviation. [`easytest.sh`](./easytest.sh) now asks for the NFM deviation mode before transmission: "_**Wide**_" uses +-5 kHz deviation for 25 kHz amateur VHF/UHF channels, while "_**Narrow**_" uses +-2.5 kHz deviation for 12.5 kHz PMR/DMR-style channels. The shared [`Biquad`](./src/utils/dsp/biquad.h) helper now accepts an explicit pole-pair Q, so higher-order Butterworth cascades can reuse the same biquad coefficient code instead of adding module-specific filter sections.

The FM broadcast transmitter with RDS has been rewritten from the legacy PiFmRds stack into a self-contained C++20 [`pifmrds`](./src/pifmrds/) binary. The original implementation mixed C and C++ sources, kept a separate control-pipe path, used `atof` / `strtol`-style argument parsing, installed handlers for all 64 signals, and built the auxiliary `rds_wav` helper. The new version opens the input through `libsndfile`, accepts mono or stereo audio at any supported input sample rate through a reusable [`PolyphaseResampler`](./src/utils/dsp/polyphase_resampler.h), validates CLI arguments with shared [`cli`](./src/utils/cli/) helpers plus module-specific PI / PS / RT checks, and drives `librpitx::ngfmdmasync` directly at the canonical 228 kHz MPX rate. The dedicated [`FmRdsProcessor`](./src/pifmrds/fmrds_processor.h) chain applies a 30 Hz HPF, selectable 50 us / 75 us broadcast pre-emphasis, a cascaded 4th-order 15 kHz Butterworth LPF, joint AGC, and MPX composition: mono audio plus RDS, or stereo `(L+R) + 19 kHz pilot + 38 kHz DSB-SC (L-R) + 57 kHz RDS`, clamped to 75 kHz peak deviation. RDS generation is split into reusable [`RdsEncoder`](./src/pifmrds/rds_encoder.h) and [`RdsModulator`](./src/pifmrds/rds_modulator.h) components: the encoder emits EN 50067 PI / PS / RT / CT groups with CRC and block offset words, while the modulator performs differential encoding, RRC-shaped biphase pulse overlap-add, and phase-locked 57 kHz subcarrier generation. [`easytest.sh`](./easytest.sh) and [`testfmrds.sh`](./scripts/testfmrds.sh) now expose PI, PS, RT, and pre-emphasis settings, while `rds_wav` and the legacy control-pipe path were removed from the build and installation flow.

## How to contact me?
- E-mail: igor.nikolaevich.96@gmail.com
- Telegram: https://t.me/igrikxd
- LinkedIn: https://www.linkedin.com/in/igor-yatsevich/

[rpitx]: https://github.com/F5OEO/rpitx
[BTC-badge]: https://img.shields.io/badge/BTC-Make%20a%20donation-orange.svg?logo=bitcoin&style=for-the-badge
[PayPal-badge]: https://img.shields.io/badge/PayPal-Make%20a%20donation-blue.svg?logo=data:image/svg+xml;base64,PHN2ZyB2aWV3Qm94PSIwIDAgMjQgMjQiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PHBhdGggZD0iTTE5LjcxNSA2LjEzM2MuMjQ5LTEuODY2IDAtMy4xMS0uOTk5LTQuMjY2QzE3LjYzNC42MjIgMTUuNzIxIDAgMTMuMzA3IDBINi4yMzVjLS40MTggMC0uOTE2LjQ0NC0xIC44ODlMMi4zMjMgMjAuNjIyYzAgLjM1Ni4yNS44LjY2NS44aDQuMzI4bC0uMjUgMS45NTZjLS4wODQuMzU1LjE2Ni42MjIuNDk4LjYyMmgzLjY2M2MuNDE3IDAgLjgzMi0uMjY3LjkxNS0uNzExdi0uMjY3bC43NDktNC42MjJ2LS4xNzhjLjA4My0uNDQ0LjUtLjguOTE1LS44aC41YzMuNTc4IDAgNi4zMjUtMS41MSA3LjE1Ni01Ljk1NS40MTgtMS44NjcuMjUyLTMuMzc4LS43NDctNC40NDUtLjI1LS4zNTUtLjY2Ni0uNjIyLTEtLjg4OSIgZmlsbD0iIzAwOWNkZSIvPjxwYXRoIGQ9Ik0xOS43MTUgNi4xMzNjLjI0OS0xLjg2NiAwLTMuMTEtLjk5OS00LjI2NkMxNy42MzQuNjIyIDE1LjcyMSAwIDEzLjMwNyAwSDYuMjM1Yy0uNDE4IDAtLjkxNi40NDQtMSAuODg5TDIuMzIzIDIwLjYyMmMwIC4zNTYuMjUuOC42NjUuOGg0LjMyOGwxLjE2NC03LjM3OC0uMDgzLjI2N2MuMDg0LS41MzMuNS0uODg5Ljk5OC0uODg5aDIuMDhjNC4wNzkgMCA3LjI0MS0xLjc3OCA4LjI0LTYuNzU1LS4wODMtLjI2NyAwLS4zNTYgMC0uNTM0IiBmaWxsPSIjMDEyMTY5Ii8+PHBhdGggZD0iTTkuNTYzIDYuMTMzYy4wODItLjI2Ni4yNS0uNTMzLjQ5OC0uNzEuMTY2IDAgLjI1LS4wOS40MTYtLjA5aDUuNDk0Yy42NjYgMCAxLjMzLjA5IDEuODMuMTc4LjE2NiAwIC4zMzMgMCAuNDk4LjA4OS4xNjguMDg5LjMzNC4wODkuNDE4LjE3OGguMjVjLjI0OC4wODkuNDk3LjI2Ni43NDguMzU1LjI0OC0xLjg2NiAwLTMuMTEtLjk5OS00LjM1NUMxNy43MTcuNTMzIDE1LjgwNCAwIDEzLjM5IDBINi4yMzVjLS40MTggMC0uOTE2LjM1Ni0xIC44ODlMMi4zMjMgMjAuNjIyYzAgLjM1Ni4yNS44LjY2NS44aDQuMzI4bDEuMTY0LTcuMzc4IDEuMDg0LTcuOTF6IiBmaWxsPSIjMDAzMDg3Ii8+PC9zdmc+&style=for-the-badge
[Revolut-badge]: https://img.shields.io/badge/Revolut-Make%20a%20donation-black.svg?logo=data:image/svg+xml;base64,PHN2ZyByb2xlPSJpbWciIHZpZXdCb3g9IjAgMCAyNCAyNCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj48dGl0bGU+UmV2b2x1dDwvdGl0bGU+PHBhdGggZD0iTTIwLjkxMzMgNi45NTY2QzIwLjkxMzMgMy4xMjA4IDE3Ljc4OTggMCAxMy45NTAzIDBIMi40MjR2My44NjA1aDEwLjk3ODJjMS43Mzc2IDAgMy4xNzcgMS4zNjUxIDMuMjA4NyAzLjA0My4wMTYuODQtLjI5OTQgMS42MzMtLjg4NzggMi4yMzI0LS41ODg2LjU5OTgtMS4zNzUuOTMwMy0yLjIxNDQuOTMwM0g5LjIzMjJhLjI3NTYuMjc1NiAwIDAgMC0uMjc1NS4yNzUydjMuNDMxYzAgLjA1ODUuMDE4LjExNDIuMDUyLjE2MTJMMTYuMjY0NiAyNGg1LjMxMTRsLTcuMjcyNy0xMC4wOTRjMy42NjI1LS4xODM4IDYuNjEtMy4yNjEyIDYuNjEtNi45NDk0ek02Ljg5NDMgNS45MjI5SDIuNDI0VjI0aDQuNDcwNHoiLz48L3N2Zz4=&style=for-the-badge
[rpitx-ui-build-badge]: https://img.shields.io/github/actions/workflow/status/IgrikXD/rpitx-ui/rpitx-ui-build.yml?&longCache=true&style=for-the-badge&label=rpitx-ui%20build&logo=raspberry-pi
