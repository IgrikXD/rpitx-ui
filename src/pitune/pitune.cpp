/**
 * @file pitune.cpp
 * @brief Unmodulated carrier transmitter implementation.
 *
 * Switches on a continuous unmodulated carrier at the requested frequency.
 * librpitx::clkgpio programs the carrier straight into the PLL that feeds
 * the clock generator of the carrier GPIO, so no DMA engine is involved.
 * The carrier stays on until SIGTERM / SIGINT (the rpitx-ui launcher stops
 * the process centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pitune --freq <Hz> [-h | --help]
 *   - --freq      Carrier frequency in Hz
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

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"

namespace pitune {
    namespace {
        /**
         * @brief Atomic shutdown flag accessed from a signal handler.
         *
         * Uses int (not bool) because std::atomic<bool> is not always lock-free
         * on 32-bit ARMv6 (default Raspberry Pi OS 32-bit target). The
         * static_assert below fails fast on any platform where the chosen type
         * is not lock-free.
         */
        std::atomic<int> running{1};
        static_assert(std::atomic<int>::is_always_lock_free,
                      "std::atomic<int> must be lock-free for signal-handler access");
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], TuneParameters& params) {
        CLI::App app{"Unmodulated carrier transmitter (librpitx clkgpio)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
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

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);

        std::cout << "pitune: carrier=" << params.transmissionFrequency << " Hz" << std::endl;

        // Wrap the carrier setup in a try / catch: programCarrierPll refuses the
        // PLL settings the board cannot run with an exception, and a stray throw
        // escaping main would terminate via std::terminate without the destructor
        // of clkgpio stopping the clock generator.
        try {
            clkgpio carrier;
            // The PLL multiplier is programmed directly; the core and SD card clocks leave the PLL first.
            carrier.SetAdvancedPllMode(true);
            // Programs the PLL and starts the clock generator; the carrier GPIO is still an input.
            programCarrierPll(carrier, params.transmissionFrequency, CARRIER_HALF_BANDWIDTH_HZ);

            carrier.enableclk(CARRIER_GPIO);
            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(STOP_CHECK_INTERVAL);
            }
            carrier.disableclk(CARRIER_GPIO);
            // The destructor of clkgpio stops the clock generator.
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
