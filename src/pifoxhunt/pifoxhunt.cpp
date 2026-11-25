/**
 * @file pifoxhunt.cpp
 * @brief Amateur radio direction finding (fox hunting) transmitter implementation.
 *
 * Transmits the code of a transmitter of the classic competition of the IARU ARDF rules
 * in Morse code: the finish beacon sends MO continuously, transmitters 1 - 5 send MOE,
 * MOI, MOS, MOH or MO5 in their minute of every five minutes of the system clock, so
 * transmitters with synchronised clocks take turns. The code is keyed at 8 - 15 WPM as a
 * keyed carrier (A1A) or as a keyed carrier switched on and off by a 1000 Hz tone (A2A)
 * and repeated until SIGTERM / SIGINT, which end the transmission after the character
 * in progress.
 *
 * @note Usage: pifoxhunt --freq <Hz> [--code <code>] [--mode <mode>] [--wpm <value>] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --code      Transmitter code: MO (finish beacon, default) | MOE | MOI | MOS | MOH | MO5 (transmitters 1 - 5)
 *   - --mode      Emission mode: A1A (keyed carrier, default) | A2A (keyed carrier switched by a 1000 Hz tone)
 *   - --wpm       Keying speed in words per minute (8 - 15, 8 by default)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pifoxhunt.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "morse_encoder.h"
#include "realtime_scheduling.h"

namespace pifoxhunt {
    namespace {
        static_assert(CHAIN_START_SAMPLES <= samplesPerSymbol(MAX_WPM),
                      "the trimmed samples must lie in the silent last symbol of a character");

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

        /**
         * @brief Render the characters of code and the word space behind them, one DMA chain of samples each.
         *
         * A key-down symbol keys the carrier on for all its samples (A1A) or for every other half period of the
         * AF tone (A2A). The chain of the next character starts late, so the gap behind each one is shortened by
         * as much.
         */
        std::vector<std::vector<unsigned char>> renderCode(std::string_view code, const EmissionMode& mode,
                                                           std::size_t symbolSamples) {
            std::vector<std::vector<unsigned char>> chains;
            for (const char character: std::string{code} + ' ') {
                std::vector<unsigned char> samples;
                for (const char symbol: morseToCw(charToMorse(character).value())) {
                    for (std::size_t n{0}; n < symbolSamples; ++n) {
                        const bool toneOn{mode.toneHalfPeriod == 0 || samples.size() / mode.toneHalfPeriod % 2 == 0};
                        samples.push_back(static_cast<unsigned char>(symbol != '0' && toneOn));
                    }
                }
                samples.resize(samples.size() - CHAIN_START_SAMPLES);
                chains.push_back(std::move(samples));
            }
            return chains;
        }

        /**
         * @brief Start of the operating period of transmitter that the system clock is in, or of the next one.
         */
        std::chrono::system_clock::time_point operatingPeriodStart(const FoxTransmitter& transmitter,
                                                                   std::chrono::system_clock::time_point now) {
            const auto cycleStart{now - now.time_since_epoch() % CYCLE};
            const auto periodStart{cycleStart + (transmitter.minute - 1) * OPERATING_PERIOD};
            return now < periodStart + OPERATING_PERIOD ? periodStart : periodStart + CYCLE;
        }

        /**
         * @brief Sleep until time, checking the stop flag every STOP_CHECK_INTERVAL.
         * @return false when a stop is requested first.
         */
        bool sleepUntil(std::chrono::system_clock::time_point time) {
            while (running.load(std::memory_order_relaxed)) {
                const auto now{std::chrono::system_clock::now()};
                if (now >= time) {
                    return true;
                }
                std::this_thread::sleep_for(
                    std::min<std::chrono::system_clock::duration>(time - now, STOP_CHECK_INTERVAL));
            }
            return false;
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FoxhuntParameters& params) {
        CLI::App app{"Amateur radio direction finding (fox hunting) transmitter (Morse code -> librpitx ookburst)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);

        // The --code values and the operating period of every transmitter come from the transmitter table.
        std::vector<std::string> codes;
        std::string codeList;
        for (const FoxTransmitter& transmitter: FOX_TRANSMITTERS) {
            codes.emplace_back(transmitter.code);
            codeList +=
                (codeList.empty() ? "" : " | ") + codes.back() +
                (transmitter.minute == 0 ? " (continuously)" : " (minute " + std::to_string(transmitter.minute) + ")");
        }
        std::string code{params.transmitter->code};
        // IsMember replaces the value with the spelling of the table; its description is
        // overridden because the option description already lists the values.
        app.add_option("--code",
                       code,
                       "Transmitter code, sent in Morse code: the finish beacon transmits continuously, transmitters "
                       "1 - 5 in their minute of every " +
                           std::to_string(CYCLE.count()) + " minutes of the system clock, " + code +
                           " by default: " + codeList)
            ->transform(CLI::IsMember(codes, CLI::ignore_case).description(""));

        // The --mode values come from the mode table.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const EmissionMode& mode: EMISSION_MODES) {
            modeNames.emplace_back(mode.name);
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back() +
                        (mode.toneHalfPeriod == 0
                             ? " (keyed carrier)"
                             : " (keyed carrier switched by a " + std::to_string(AF_TONE_HZ) + " Hz tone)");
        }
        std::string modeName{params.mode->name};
        app.add_option("--mode", modeName, "Emission mode, " + modeName + " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames, CLI::ignore_case).description(""));

        app.add_option("--wpm",
                       params.wpm,
                       "Keying speed in words per minute, " + std::to_string(std::lround(params.wpm)) + " by default")
            ->check(rpitx::cli::validators::PositiveFiniteFloat)
            ->check(CLI::Range(MIN_WPM, MAX_WPM));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        params.transmitter = &*std::ranges::find(FOX_TRANSMITTERS, code, &FoxTransmitter::code);
        params.mode        = &*std::ranges::find(EMISSION_MODES, modeName, &EmissionMode::name);
        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        FoxhuntParameters params;
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
        const FoxTransmitter& transmitter{*params.transmitter};
        const EmissionMode& mode{*params.mode};

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);

        // Real-time scheduling: start the DMA chain of every character in time while other processes load the CPU.
        enableRealtimeScheduling();

        // Wrap the OOK setup and the transmission loop in a try / catch: checkCarrierPll
        // refuses the PLL settings the board cannot run with an exception. A stray throw
        // escaping main would terminate via std::terminate without the destructor of
        // ookburst stopping the clock generator.
        try {
            const std::string modeText{mode.toneHalfPeriod == 0
                                           ? std::string{mode.name}
                                           : std::string{mode.name} + " (" + std::to_string(AF_TONE_HZ) + " Hz tone)"};
            const std::string schedule{transmitter.minute == 0 ? "continuous"
                                                               : "minute " + std::to_string(transmitter.minute) +
                                                                     " of " + std::to_string(CYCLE.count())};
            std::cout << "pifoxhunt: carrier=" << params.transmissionFrequency << " Hz, code=" << transmitter.code
                      << ", mode=" << modeText << ", wpm=" << params.wpm << ", schedule=" << schedule << std::endl;

            const std::size_t symbolSamples{samplesPerSymbol(params.wpm)};
            auto chains{renderCode(transmitter.code, mode, symbolSamples)};
            // The next chain starts late by the samples cut from each one, so the code lasts all its symbols.
            std::size_t codeSamples{0};
            std::size_t longestChainSamples{0};
            for (const auto& chain: chains) {
                codeSamples += chain.size() + CHAIN_START_SAMPLES;
                longestChainSamples = std::max(longestChainSamples, chain.size());
            }
            const auto codeDuration{std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>{static_cast<double>(codeSamples) / SAMPLE_RATE})};

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, 0);
            ookburst ook{params.transmissionFrequency,
                         static_cast<float>(SAMPLE_RATE),
                         DMA_CHANNEL,
                         static_cast<uint32_t>(longestChainSamples)};

            // Send the code over and over: continuously, or while it fits into the operating periods of the cycle.
            while (running.load(std::memory_order_relaxed)) {
                auto periodEnd{std::chrono::system_clock::time_point::max()};
                if (transmitter.minute != 0) {
                    const auto periodStart{operatingPeriodStart(transmitter, std::chrono::system_clock::now())};
                    periodEnd = periodStart + OPERATING_PERIOD;
                    if (sleepUntil(periodStart) == false) {
                        break;
                    }
                }
                while (running.load(std::memory_order_relaxed) &&
                       (transmitter.minute == 0 || std::chrono::system_clock::now() + codeDuration <= periodEnd)) {
                    for (auto& chain: chains) {
                        if (running.load(std::memory_order_relaxed) == 0) {
                            break;
                        }
                        // SetSymbols returns once the chain is sent, so a stop request ends the code there.
                        ook.SetSymbols(chain.data(), static_cast<uint32_t>(chain.size()));
                    }
                }
                // The code no longer fits: the next operating period begins after this one.
                if (transmitter.minute != 0 && sleepUntil(periodEnd) == false) {
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pifoxhunt: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pifoxhunt: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pifoxhunt

int main(int argc, char* argv[]) {
    return pifoxhunt::run(argc, argv);
}
