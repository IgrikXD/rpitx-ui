/**
 * @file pifoxhunt.cpp
 * @brief Amateur radio direction finding (fox hunting) transmitter implementation.
 *
 * Transmits the code of a transmitter of the classic competition of the IARU ARDF rules
 * in Morse code: the finish beacon sends MO continuously, transmitters 1 - 5 send MOE,
 * MOI, MOS, MOH or MO5 in their minute of every five minutes of the system clock, so
 * transmitters with synchronised clocks take turns. The code is keyed at 8 - 15 WPM as a
 * keyed carrier (A1A) or as a keyed carrier modulated by a 1000 Hz tone (A2A) and
 * repeated until a stop signal. With a callsign, DE and the callsign follow the last
 * code of every minute the transmitter operates in, every minute of the system clock for
 * the finish beacon.
 *
 * @note Usage: pifoxhunt --freq <Hz> [--code <code>] [--mode <mode>] [--wpm <value>] [--callsign <callsign>]
 *               [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --code      Transmitter code: MO (finish beacon, default) | MOE | MOI | MOS | MOH | MO5 (transmitters 1 - 5)
 *   - --mode      Emission mode: A1A (keyed carrier, default) | A2A (keyed carrier modulated by a 1000 Hz tone)
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
#include <complex>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "morse_encoder.h"
#include "realtime_scheduling.h"
#include "stop_signals.h"

namespace pifoxhunt {
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

        static_assert(
            DMA_FIFO_SIZE / 4 + DMA_BLOCK_SAMPLES <= 7 * samplesPerSymbol(MAX_WPM),
            "the samples queued ahead of a code may delay it by no more than the 7 silent symbols that end it");

        /**
         * @brief Render the characters of text and the word space behind them as iqdmasync MODE_FREQ_A samples.
         *
         * A key-down symbol drives the carrier at the level of the half period of the AF tone, which restarts
         * with the first half period at every element; a key-up symbol keys the carrier off.
         */
        [[nodiscard]] std::vector<std::complex<float>> renderCode(std::string_view text, const EmissionMode& mode,
                                                                  std::size_t symbolSamples) {
            std::vector<std::complex<float>> samples;
            std::size_t elementSamples{0};
            for (const char character: std::string{text} + ' ') {
                for (const char symbol: morseToCw(charToMorse(character).value())) {
                    if (symbol == '0') {
                        elementSamples = 0;
                        samples.insert(samples.end(), symbolSamples, CARRIER_OFF_SAMPLE);
                        continue;
                    }
                    for (std::size_t n{0}; n < symbolSamples; ++n) {
                        const bool firstHalf{elementSamples / TONE_HALF_PERIOD % 2 == 0};
                        samples.emplace_back(
                            0.0F, static_cast<float>(firstHalf ? mode.firstHalfLevel : mode.secondHalfLevel));
                        ++elementSamples;
                    }
                }
            }
            return samples;
        }

        /**
         * @brief Time the DMA engine takes to transmit samples.
         */
        [[nodiscard]] std::chrono::system_clock::duration sendingTime(const std::vector<std::complex<float>>& samples) {
            return std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>{static_cast<double>(samples.size()) / SAMPLE_RATE});
        }

        /**
         * @brief Start of the operating period of transmitter that contains now, or of the next one: the minute of
         *        the cycle of transmitters 1 - 5, every minute for the finish beacon.
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
        CLI::App app{
            "Amateur radio direction finding (fox hunting) transmitter (transmitter code -> librpitx iqdmasync)"};

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
            modeList +=
                (modeList.empty() ? "" : " | ") + modeNames.back() +
                (isToneModulated(mode) ? " (keyed carrier modulated by a " + std::to_string(AF_TONE_HZ) + " Hz tone)"
                                       : " (keyed carrier)");
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

        // Stop signals: end the transmission cleanly, also when the terminal or the output pipe closes.
        installStopSignalHandlers(handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

        // Wrap the rendering, DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll refuses the PLL settings the board cannot run with an exception.
        // A stray throw escaping main would terminate via std::terminate without the
        // destructor of iqdmasync stopping the DMA engine and the clock generator.
        try {
            const std::string modeText{isToneModulated(mode)
                                           ? std::string{mode.name} + " (" + std::to_string(AF_TONE_HZ) + " Hz tone)"
                                           : std::string{mode.name}};
            const std::string schedule{transmitter.minute == 0 ? "continuous"
                                                               : "minute " + std::to_string(transmitter.minute) +
                                                                     " of " + std::to_string(CYCLE.count())};
            std::cout << "pifoxhunt: carrier=" << params.transmissionFrequency << " Hz, code=" << transmitter.code
                      << ", mode=" << modeText << ", wpm=" << params.wpm << ", schedule=" << schedule
                      << (params.callsign.empty() ? "" : ", callsign=" + params.callsign) << std::endl;

            const std::size_t symbolSamples{samplesPerSymbol(params.wpm)};
            auto codeSamples{renderCode(transmitter.code, mode, symbolSamples)};
            auto idSamples{params.callsign.empty() ? std::vector<std::complex<float>>{}
                                                   : renderCode("DE " + params.callsign, mode, symbolSamples)};
            const auto codeDuration{sendingTime(codeSamples)};
            const auto idDuration{sendingTime(idSamples)};
            // The finish beacon without identification transmits without a break, the others in their periods.
            const bool scheduled{transmitter.minute != 0 || idSamples.empty() == false};

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            // Constructed with the first code, so the carrier its constructor switches on falls into the period.
            std::optional<iqdmasync> dma;

            // Queue samples in the DMA engine block by block, until a stop is requested.
            const auto send{[&dma](std::vector<std::complex<float>>& samples) {
                for (std::size_t first{0}; first < samples.size() && running.load(std::memory_order_relaxed);
                     first += DMA_BLOCK_SAMPLES) {
                    // SetIQSamples blocks until the block is in the DMA FIFO (see
                    // iqdmasync.cpp upstream), so no manual pacing is needed here.
                    dma->SetIQSamples(
                        samples.data() + first, std::min(DMA_BLOCK_SAMPLES, samples.size() - first), DEFAULT_HARMONIC);
                }
            }};
            std::vector<std::complex<float>> idleBlock(DMA_BLOCK_SAMPLES, CARRIER_OFF_SAMPLE);

            // Send the code over and over: continuously, or while it and the identification fit into the period.
            while (running.load(std::memory_order_relaxed)) {
                bool lastCode{false};
                if (scheduled) {
                    const auto now{std::chrono::system_clock::now()};
                    const auto periodStart{operatingPeriodStart(transmitter, now)};
                    const auto periodEnd{periodStart + OPERATING_PERIOD};
                    if (now < periodStart || now + codeDuration + idDuration > periodEnd) {
                        if (dma.has_value()) {
                            // The engine keeps the carrier off between the operating periods.
                            send(idleBlock);
                        } else {
                            const auto wakeUp{now < periodStart ? periodStart
                                                                : operatingPeriodStart(transmitter, periodEnd)};
                            std::this_thread::sleep_for(
                                std::min<std::chrono::system_clock::duration>(wakeUp - now, STOP_CHECK_INTERVAL));
                        }
                        continue;
                    }
                    // The identification follows the last code of the period.
                    lastCode = now + 2 * codeDuration + idDuration + NEXT_CODE_TOLERANCE > periodEnd;
                }
                if (dma.has_value() == false) {
                    dma.emplace(params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE, MODE_FREQ_A);
                    // Slot 0 and the start-up burst of the engine get carrier-off samples, so the first symbol
                    // reaches the air.
                    startTransmission(*dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC);
                }
                send(codeSamples);
                if (lastCode) {
                    send(idSamples);
                }
            }

            // The transmission ends only on a stop request, so the queued samples are discarded.
            if (dma.has_value()) {
                stopTransmission(*dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC, false);
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
