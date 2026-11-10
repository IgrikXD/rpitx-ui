/**
 * @file pitune.cpp
 * @brief Unmodulated carrier transmitter implementation.
 *
 * Switches on a continuous unmodulated carrier at the requested frequency.
 * librpitx::clkgpio programs the carrier straight into the PLL that feeds
 * the clock generator of the carrier GPIO, so no DMA engine is involved.
 * The carrier stays on until SIGTERM / SIGINT, or SIGHUP when the terminal
 * goes away (the rpitx-ui launcher stops the process centrally via killall
 * when the user dismisses the dialog).
 *
 * @note Usage: pitune --freq <Hz> [--ppm <value>] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --ppm       Crystal error in ppm, positive when the carrier comes out low (default: the NTP estimate)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pitune.h"

#include <librpitx/librpitx.h>
#include <signal.h>
#include <sys/timex.h>

#include <CLI/CLI.hpp>
#include <charconv>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>

#include "cli_common.h"
#include "cli_validators.h"

namespace pitune {
    namespace {
        /**
         * @brief Validator that accepts only finite clock corrections within +-MAX_PPM.
         */
        const CLI::Validator ClockCorrectionPpm{
            [](const std::string& text) -> std::string {
                double value{};
                const auto first{text.data()};
                const auto last{first + text.size()};
                if (const auto [ptr, ec]{std::from_chars(first, last, value)};
                    ptr != last || ec == std::errc::invalid_argument) {
                    return "must be a numeric value";
                } else if (ec == std::errc::result_out_of_range || std::isfinite(value) == false ||
                           std::fabs(value) > MAX_PPM) {
                    return "must be a finite value in [-" + std::to_string(MAX_PPM) + ", " + std::to_string(MAX_PPM) +
                           "]";
                }
                return {};
            },
            ""};

        /**
         * @brief Carrier clock generator with read access to the clock correction it applies.
         */
        class CarrierClock : public clkgpio {
        public:
            [[nodiscard]] double clockCorrectionPpm() const {
                return clk_ppm;
            }
        };

        /**
         * @brief Choose the PLL divider for the carrier, refusing the settings the board cannot run.
         * @return The PLL rate of the chosen setting in Hz, std::nullopt when the carrier was refused
         *         (the error is printed).
         */
        [[nodiscard]] std::optional<double> choosePllSetting(clkgpio& carrier, uint64_t frequency) {
            // clkgpio::SetCenterFrequency runs ComputeBestLO on the PLL frequency, which is twice the
            // carrier on BCM2711 (fixed post-divider), so the same argument is prepared here to check
            // the divider before the PLL is programmed.
            const uint64_t pllFrequency{frequency * (carrier.pi_is_2711 ? 2U : 1U)};

            // ComputeBestLO only selects the divider; it fails outside the carrier range of the PLL (and
            // keeps the previous divider), and for a low carrier even its largest divider runs the PLL
            // below the minimum - either setting freezes the board, so refuse before programming it.
            if (carrier.ComputeBestLO(pllFrequency, PLL_GUARD_BAND_HZ) != 0) {
                std::cerr << "[ERROR] pitune: no PLL setting generates a " << frequency << " Hz carrier on this board."
                          << std::endl;
                return std::nullopt;
            }
            // Divider 1 stands for the feedback pre-divider of the PLL, which doubles its rate.
            const int pllMultiplier{carrier.PllFixDivider == 1 ? 2 : carrier.PllFixDivider};
            const double pllRate{static_cast<double>(pllFrequency) * pllMultiplier};
            if (pllRate < MIN_PLL_RATE_HZ) {
                std::cerr << "[ERROR] pitune: no PLL setting generates a " << frequency
                          << " Hz carrier on this board (the PLL would run at " << pllRate / 1e6 << " MHz, below its "
                          << MIN_PLL_RATE_HZ / 1e6 << " MHz minimum); use a higher --freq." << std::endl;
                return std::nullopt;
            }
            return pllRate;
        }

        /**
         * @brief Whether the kernel holds the NTP estimate of the crystal error that librpitx applies by default.
         */
        [[nodiscard]] bool ntpEstimateApplied() {
            timex clockStatus{};
            return ntp_adjtime(&clockStatus) == TIME_OK;
        }

        /**
         * @brief Signals that end the transmission: SIGTERM, SIGINT and, unless it is ignored (nohup), SIGHUP.
         */
        [[nodiscard]] sigset_t stopSignalSet() {
            sigset_t stopSignals;
            sigemptyset(&stopSignals);
            sigaddset(&stopSignals, SIGTERM);
            sigaddset(&stopSignals, SIGINT);
            struct sigaction hangupAction{};
            if (sigaction(SIGHUP, nullptr, &hangupAction) != 0 || hangupAction.sa_handler != SIG_IGN) {
                sigaddset(&stopSignals, SIGHUP);
            }
            return stopSignals;
        }

        /**
         * @brief Block until one of the stop signals arrives.
         * @return True when a stop signal was taken (the error is printed otherwise).
         */
        [[nodiscard]] bool waitForStopSignal(const sigset_t& stopSignals) {
            int stopSignal{0};
            if (const int status{sigwait(&stopSignals, &stopSignal)}; status != 0) {
                std::cerr << "[ERROR] pitune: waiting for a stop signal failed ("
                          << std::error_code{status, std::system_category()}.message() << ")." << std::endl;
                return false;
            }
            return true;
        }
    }  // namespace

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], TuneParameters& params) {
        CLI::App app{"Unmodulated carrier transmitter (librpitx clkgpio)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--ppm",
                       params.ppm,
                       "Crystal error in ppm, positive when the carrier comes out low (default: the NTP estimate of "
                       "the kernel, range [-" +
                           std::to_string(MAX_PPM) + ", " + std::to_string(MAX_PPM) + "])")
            ->check(ClockCorrectionPpm);

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (const auto result{rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // No board has a PLL setting above the limit, and the check of librpitx overflows far above it.
        if (params.transmissionFrequency > MAX_FREQUENCY_HZ) {
            std::cerr << "[ERROR] --freq (" << params.transmissionFrequency << " Hz) must not exceed "
                      << MAX_FREQUENCY_HZ << " Hz!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        return rpitx::cli::ParseResult::Ok;
    }

    int run(int argc, char* argv[]) {
        TuneParameters params;
        // No default branch: ParseResult is closed-set, so -Wswitch flags any future
        // enumerator that forgets to update this dispatch.
        switch (parseArgs(argc, argv, params)) {
            case rpitx::cli::ParseResult::Ok:
                break;
            case rpitx::cli::ParseResult::Help:
                return 0;
            case rpitx::cli::ParseResult::Error:
                return 1;
        }

        // There is nothing to do while the carrier is on, so the stop signals are blocked here and
        // taken with sigwait once it is on: no polling, and the clock setup is never cut short.
        const sigset_t stopSignals{stopSignalSet()};
        if (const int status{pthread_sigmask(SIG_BLOCK, &stopSignals, nullptr)}; status != 0) {
            std::cerr << "[ERROR] pitune: blocking the stop signals failed ("
                      << std::error_code{status, std::system_category()}.message() << ")." << std::endl;
            return 1;
        }

        // Wrap the carrier setup in a try / catch so that an exception leaves through the destructor
        // of clkgpio, which stops the clock generator, instead of std::terminate.
        try {
            CarrierClock carrier;
            // The PLL multiplier is programmed directly; the core and SD card clocks leave the PLL first.
            carrier.SetAdvancedPllMode(true);
            // SetAdvancedPllMode re-applied the NTP estimate; an explicit --ppm replaces it after that call.
            if (params.ppm != std::nullopt) {
                carrier.Setppm(params.ppm.value());
            }
            const auto pllRate{choosePllSetting(carrier, params.transmissionFrequency)};
            if (pllRate == std::nullopt) {
                return 1;
            }
            // Programs the PLL and starts the clock generator; the carrier GPIO is still an input.
            carrier.SetCenterFrequency(params.transmissionFrequency, PLL_GUARD_BAND_HZ);

            std::cout << "pitune: carrier=" << params.transmissionFrequency
                      << " Hz, ppm=" << carrier.clockCorrectionPpm();
            if (params.ppm == std::nullopt) {
                std::cout << (ntpEstimateApplied() ? " (ntp)" : " (no ntp estimate)");
            }
            std::cout << ", pll_rate=" << pllRate.value() / 1e6 << " MHz" << std::endl;
            if ((carrier.gpioreg[CM_LOCK] & CM_LOCK_FLOCKC) == 0) {
                std::cerr << "[WARNING] pitune: the carrier PLL reports no lock; the carrier may be off frequency."
                          << std::endl;
            }

            carrier.enableclk(CARRIER_GPIO);
            const bool stopRequested{waitForStopSignal(stopSignals)};
            carrier.disableclk(CARRIER_GPIO);
            // The destructor of clkgpio stops the clock generator.
            if (stopRequested == false) {
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pitune: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pitune: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pitune

int main(int argc, char* argv[]) {
    return pitune::run(argc, argv);
}
