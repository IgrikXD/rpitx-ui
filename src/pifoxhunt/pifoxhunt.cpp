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
 * in progress. With a callsign, DE and the callsign follow the last code of every minute
 * the transmitter operates in, every minute of the system clock for the finish beacon.
 *
 * @note Usage: pifoxhunt --freq <Hz> [--code <code>] [--mode <mode>] [--wpm <value>] [--callsign <callsign>]
 *               [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --code      Transmitter code: MO (finish beacon, default) | MOE | MOI | MOS | MOH | MO5 (transmitters 1 - 5)
 *   - --mode      Emission mode: A1A (keyed carrier, default) | A2A (keyed carrier switched by a 1000 Hz tone)
 *   - --wpm       Keying speed in words per minute (8 - 15, 8 by default)
 *   - --callsign  Callsign sent as DE <callsign> at the end of every minute of operation (1 - 15 letters, digits or /)
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
#include <cctype>
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
         * @brief Render the characters of text and the word space behind them, one DMA chain of samples each.
         *
         * A key-down symbol keys the carrier on for all its samples (A1A) or for every other half period of the
         * AF tone (A2A). The chain of the next character starts late, so the gap behind each one is shortened by
         * as much.
         */
        [[nodiscard]] std::vector<std::vector<unsigned char>> renderCode(std::string_view text,
                                                                         const EmissionMode& mode,
                                                                         std::size_t symbolSamples) {
            std::vector<std::vector<unsigned char>> chains;
            for (const char character: std::string{text} + ' ') {
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
         * @brief Time ookburst takes to send chains one after the other, each one starting late by the samples cut
         *        from the one before.
         */
        [[nodiscard]] std::chrono::system_clock::duration sendingTime(
            const std::vector<std::vector<unsigned char>>& chains) {
            std::size_t samples{0};
            for (const auto& chain: chains) {
                samples += chain.size() + CHAIN_START_SAMPLES;
            }
            return std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>{static_cast<double>(samples) / SAMPLE_RATE});
        }

        /**
         * @brief Start of the operating period of transmitter that the system clock is in, or of the next one: the
         *        minute of the cycle of transmitters 1 - 5, every minute for the finish beacon.
         */
        [[nodiscard]] std::chrono::system_clock::time_point operatingPeriodStart(
            const FoxTransmitter& transmitter, std::chrono::system_clock::time_point now) {
            if (transmitter.minute == 0) {
                return now - now.time_since_epoch() % OPERATING_PERIOD;
            }
            const auto cycleStart{now - now.time_since_epoch() % CYCLE};
            const auto periodStart{cycleStart + (transmitter.minute - 1) * OPERATING_PERIOD};
            return now < periodStart + OPERATING_PERIOD ? periodStart : periodStart + CYCLE;
        }

        /**
         * @brief Whether callsign is 1 - MAX_CALLSIGN_LENGTH letters, digits or /.
         */
        [[nodiscard]] bool isCallsign(std::string_view callsign) {
            return callsign.empty() == false && callsign.size() <= MAX_CALLSIGN_LENGTH &&
                   std::ranges::all_of(callsign, [](char ch) {
                       return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '/';
                   });
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

        const auto* callsignOption{app.add_option(
            "--callsign",
            params.callsign,
            "Callsign sent as DE <callsign> behind the last code of every minute the transmitter operates in, every "
            "minute for the finish beacon: 1 - " +
                std::to_string(MAX_CALLSIGN_LENGTH) +
                " letters, digits or /, letters are sent in upper case; no identification by default")};

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (callsignOption->count() > 0 && isCallsign(params.callsign) == false) {
            std::cerr << "[ERROR] --callsign must be 1 - " << MAX_CALLSIGN_LENGTH << " letters, digits or /!"
                      << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        std::ranges::transform(params.callsign, params.callsign.begin(), [](char ch) {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        });

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
                      << ", mode=" << modeText << ", wpm=" << params.wpm << ", schedule=" << schedule
                      << (params.callsign.empty() ? "" : ", callsign=" + params.callsign) << std::endl;

            const std::size_t symbolSamples{samplesPerSymbol(params.wpm)};
            auto codeChains{renderCode(transmitter.code, mode, symbolSamples)};
            auto idChains{params.callsign.empty() ? std::vector<std::vector<unsigned char>>{}
                                                  : renderCode("DE " + params.callsign, mode, symbolSamples)};
            const auto codeDuration{sendingTime(codeChains)};
            const auto idDuration{sendingTime(idChains)};
            std::size_t longestChainSamples{0};
            for (const auto& chain: codeChains) {
                longestChainSamples = std::max(longestChainSamples, chain.size());
            }
            for (const auto& chain: idChains) {
                longestChainSamples = std::max(longestChainSamples, chain.size());
            }

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, 0);
            ookburst ook{params.transmissionFrequency,
                         static_cast<float>(SAMPLE_RATE),
                         DMA_CHANNEL,
                         static_cast<uint32_t>(longestChainSamples)};

            // SetSymbols returns once a chain is sent, so a stop request ends the transmission there.
            const auto send{[&ook](std::vector<std::vector<unsigned char>>& chains) {
                for (auto& chain: chains) {
                    if (running.load(std::memory_order_relaxed) == 0) {
                        break;
                    }
                    ook.SetSymbols(chain.data(), static_cast<uint32_t>(chain.size()));
                }
            }};

            // Send the code over and over: continuously, or while it and the identification fit into the period.
            while (running.load(std::memory_order_relaxed)) {
                bool lastCode{false};
                if (transmitter.minute != 0 || idChains.empty() == false) {
                    const auto now{std::chrono::system_clock::now()};
                    const auto periodStart{operatingPeriodStart(transmitter, now)};
                    const auto periodEnd{periodStart + OPERATING_PERIOD};
                    if (now < periodStart || now + codeDuration + idDuration > periodEnd) {
                        const auto wakeUp{now < periodStart ? periodStart
                                                            : operatingPeriodStart(transmitter, periodEnd)};
                        std::this_thread::sleep_for(
                            std::min<std::chrono::system_clock::duration>(wakeUp - now, STOP_CHECK_INTERVAL));
                        continue;
                    }
                    // The identification follows the last code of the period.
                    lastCode = now + 2 * codeDuration + idDuration > periodEnd;
                }
                send(codeChains);
                if (lastCode) {
                    send(idChains);
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
