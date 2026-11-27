/**
 * @file piook.cpp
 * @brief On-off keying (OOK) transmitter implementation.
 *
 * Keys the carrier on and off as the message states it. On-off keying is a modulation, not
 * a protocol: the message is the keying sequence itself - the pulses and the gaps between
 * them - written as a string of 1 and 0 with a symbol time or as the durations of the
 * pulses and gaps, so it can carry the frame of any protocol in any line code. The sequence
 * is sent the requested number of times with a pause between the repetitions, and the DMA
 * engine times the whole transmission on a grid of 10 us. Transmission ends with the last
 * repetition, or on a stop signal.
 *
 * @note Usage: piook --freq <Hz> --message "<sequence>" [--type symbols|timings] [--symbol-time <us>]
 *               [--repeat <count>] [--pause <us>] [-h | --help]
 *   - --freq         Carrier frequency in Hz
 *   - --message      Keying sequence of the carrier, written as --type says (quote it)
 *   - --type         Message type: symbols (default, 1 and 0) | timings (durations in microseconds)
 *   - --symbol-time  Duration of a symbol of --type symbols in microseconds
 *   - --repeat       Number of times the sequence is sent (1 by default)
 *   - --pause        Pause between the repetitions in microseconds (none by default)
 *   - -h, --help     Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "piook.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "stop_signals.h"

namespace piook {
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

        /**
         * @brief Carrier-off samples ahead of the sequence: the pacing of the DMA engine settles in them, so
         *        the first pulse has its whole length.
         */
        constexpr uint32_t LEAD_SAMPLES{rpitx::dma::PCM_FIFO_DEPTH};

        /**
         * @brief Longest transmission the DMA chain holds, in microseconds.
         */
        constexpr double MAX_TRANSMISSION_TIME{static_cast<double>(MAX_CHAIN_RUNS) * MAX_RUN_SAMPLES * SAMPLE_TIME_US};

        /**
         * @brief Position of the function of the carrier GPIO in the GPFSEL0 register: three bits per pin.
         */
        constexpr uint32_t CARRIER_FUNCTION_SHIFT{3 * rpitx::dma::CARRIER_GPIO};

        /**
         * @brief Run of the DMA chain: the carrier keyed on or off for a number of samples.
         */
        struct KeyingRun {
            bool carrierOn;
            uint32_t samples;
        };

        /**
         * @brief librpitx ookburst whose DMA chain holds every run of equal samples in one control block pair.
         *
         * ookburst paces every sample with a control block of its own. Here the pacing block of a run writes
         * as many words as the run has samples, so the chain grows with the number of pulses and gaps, not
         * with the duration of the transmission.
         */
        class RunLengthOokBurst : public ookburst {
        public:
            /**
             * @param frequency Carrier frequency in Hz.
             * @param runs      Runs of the transmission, at most MAX_RUN_SAMPLES samples each.
             */
            RunLengthOokBurst(uint64_t frequency, const std::vector<KeyingRun>& runs)
                : ookburst{
                      frequency, static_cast<float>(SAMPLE_RATE), DMA_CHANNEL, static_cast<uint32_t>(runs.size())} {
                // The chain ookburst has built for one sample per run: a block that fills the pacing FIFO, per
                // sample a block that writes the function of the carrier GPIO and a block that paces it with one
                // word, then a block that makes the pin an input.
                const uint32_t otherPins{Originfsel & ~(0b111U << CARRIER_FUNCTION_SHIFT)};
                for (std::size_t run{0}; run < runs.size(); ++run) {
                    const uint32_t carrierFunction{runs[run].carrierOn ? fsel_alt0 : fsel_input};
                    sampletab[run]              = otherPins | (carrierFunction << CARRIER_FUNCTION_SHIFT);
                    cbarray[2 * run + 2].length = sizeof(uint32_t) * runs[run].samples;
                }
            }
        };

        /**
         * @brief Runs of the DMA chain of a transmission: the lead, then sequence sent repeat times with
         *        pause between the repetitions.
         *
         * Every edge is rounded to the nearest sample of the time line of the whole transmission, so the
         * rounding does not add up; a pulse or a gap longer than MAX_RUN_SAMPLES takes several runs.
         *
         * @throws std::runtime_error when the transmission does not fit into MAX_CHAIN_RUNS runs.
         */
        [[nodiscard]] std::vector<KeyingRun> keyingRuns(const std::vector<KeyingInterval>& sequence, uint32_t repeat,
                                                        double pause) {
            const auto refuse{[] {
                throw std::runtime_error{"the transmission needs more than " + std::to_string(MAX_CHAIN_RUNS) +
                                         " runs of the DMA chain (one per pulse or gap, one more for every " +
                                         std::to_string(MAX_RUN_SAMPLES * SAMPLE_TIME_US) +
                                         " us of it); use fewer repetitions, a shorter pause or a shorter sequence"};
            }};

            // Refused before the repetitions are laid out: a count far beyond the chain would take long to walk.
            double transmissionTime{(repeat - 1) * pause};
            for (const KeyingInterval& interval: sequence) {
                transmissionTime += repeat * interval.duration;
            }
            if (transmissionTime > MAX_TRANSMISSION_TIME) {
                refuse();
            }

            std::vector<KeyingRun> runs{{false, LEAD_SAMPLES}};
            double time{0.0};      // Microseconds from the start of the first repetition.
            uint64_t position{0};  // Samples sent up to time.
            const auto append{[&](bool carrierOn, double duration) {
                time += duration;
                const double end{std::round(time / SAMPLE_TIME_US)};
                for (uint64_t samples{static_cast<uint64_t>(end) - position}; samples > 0;) {
                    if (runs.back().carrierOn != carrierOn || runs.back().samples == MAX_RUN_SAMPLES) {
                        if (runs.size() == MAX_CHAIN_RUNS) {
                            refuse();
                        }
                        runs.push_back({carrierOn, 0});
                    }
                    const auto added{std::min<uint64_t>(samples, MAX_RUN_SAMPLES - runs.back().samples)};
                    runs.back().samples += static_cast<uint32_t>(added);
                    samples -= added;
                }
                position = static_cast<uint64_t>(end);
            }};

            for (uint32_t repetition{0}; repetition < repeat; ++repetition) {
                if (repetition > 0) {
                    append(false, pause);
                }
                for (const KeyingInterval& interval: sequence) {
                    append(interval.carrierOn, interval.duration);
                }
            }
            return runs;
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], OokParameters& params) {
        CLI::App app{"On-off keying (OOK) transmitter (keying sequence -> librpitx ookburst)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        std::string message;
        app.add_option("--message",
                       message,
                       "Keying sequence of the carrier, the pulses and gaps of a frame of any protocol, written as "
                       "--type says (quote it, e.g. --message \"1010 1100\")")
            ->required();
        // IsMember replaces the value with the spelling of the table; its description is
        // overridden because the option description already lists the values.
        const std::vector<std::string> typeNames{TYPE_NAMES.begin(), TYPE_NAMES.end()};
        std::string type{typeName(params.type)};
        // Shortest pulse, gap, symbol or pause in microseconds, as --help and the diagnostics name it.
        const std::string shortest{std::to_string(SAMPLE_TIME_US)};
        app.add_option("--type",
                       type,
                       "Message type: symbols (default, a string of 1 and 0 - the carrier is on or off for "
                       "--symbol-time each, white space is ignored) | timings (durations of at least " +
                           shortest +
                           " microseconds, separated by white space or commas - a pulse for a positive and a gap "
                           "for a negative one, or pulses and gaps in turn, starting with a pulse, when none is "
                           "negative)")
            ->transform(CLI::IsMember(typeNames, CLI::ignore_case).description(""));
        // The durations and the count are taken as text and read as decimal numbers below: the conversions of
        // CLI11 take an empty or a hexadecimal value, and the leading zero of a count for an octal prefix.
        std::string symbolTimeText;
        const auto* symbolTimeOption{app.add_option(
            "--symbol-time",
            symbolTimeText,
            "Duration of a symbol in microseconds, at least " + shortest + ", required with --type symbols")};
        std::string repeatText{std::to_string(params.repeat)};
        app.add_option("--repeat", repeatText, "Number of times the sequence is sent, " + repeatText + " by default");
        std::string pauseText;
        const auto* pauseOption{app.add_option(
            "--pause",
            pauseText,
            "Pause between the repetitions in microseconds, none by default: 0 or at least " + shortest)};

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        const char* const repeatEnd{repeatText.data() + repeatText.size()};
        if (const auto [end, error]{std::from_chars(repeatText.data(), repeatEnd, params.repeat)};
            error != std::errc{} || end != repeatEnd || params.repeat == 0) {
            std::cerr << "[ERROR] --repeat must be a whole number of at least 1!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (pauseOption->count() > 0) {
            const auto pause{parseDuration(pauseText)};
            // A pause is none or lasts at least a sample, as a gap of the message does.
            if (pause == std::nullopt || std::signbit(pause.value()) ||
                (pause.value() > 0.0 && pause.value() < SAMPLE_TIME_US)) {
                std::cerr << "[ERROR] --pause must be 0 or a number of at least " << shortest << " microseconds!"
                          << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
            params.pause = pause.value();
        }

        params.type = static_cast<MessageType>(std::ranges::find(typeNames, type) - typeNames.begin());
        // Only a message of symbols has a symbol time, and no symbol time fits every protocol.
        if (params.type == MessageType::Symbols) {
            if (symbolTimeOption->count() == 0) {
                std::cerr << "[ERROR] --type symbols requires --symbol-time <microseconds>!" << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
            const auto symbolTime{parseDuration(symbolTimeText)};
            if (symbolTime == std::nullopt || symbolTime.value() < SAMPLE_TIME_US) {
                std::cerr << "[ERROR] --symbol-time must be a number of at least " << shortest << " microseconds!"
                          << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
            params.symbolTime = symbolTime.value();
        } else if (symbolTimeOption->count() > 0) {
            std::cerr << "[ERROR] --symbol-time is only valid with --type symbols!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        auto sequence{params.type == MessageType::Symbols ? parseSymbols(message, params.symbolTime)
                                                          : parseTimings(message)};
        if (sequence == std::nullopt) {
            std::cerr << "[ERROR] --message must contain only "
                      << (params.type == MessageType::Symbols
                              ? "1, 0 and white space"
                              : "durations in microseconds, separated by white space or commas,")
                      << " with --type " << typeName(params.type) << "!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (std::ranges::none_of(sequence.value(), &KeyingInterval::carrierOn)) {
            std::cerr << "[ERROR] --message must key the carrier on at least once!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (std::ranges::any_of(sequence.value(),
                                [](const KeyingInterval& interval) { return interval.duration < SAMPLE_TIME_US; })) {
            std::cerr << "[ERROR] --message must have no pulse or gap shorter than " << shortest << " microseconds!"
                      << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        params.sequence = std::move(sequence.value());

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        OokParameters params;
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

        // Wrap the DMA chain setup and the transmission in a try / catch: keyingRuns
        // refuses a transmission the DMA chain cannot hold and checkCarrierPll the PLL
        // settings the board cannot run with an exception. A stray throw escaping main
        // would terminate via std::terminate without the destructor of ookburst stopping
        // the DMA engine and the clock generator.
        try {
            const auto runs{keyingRuns(params.sequence, params.repeat, params.pause)};
            uint64_t transmissionSamples{0};
            for (const KeyingRun& run: runs) {
                transmissionSamples += run.samples;
            }
            transmissionSamples -= LEAD_SAMPLES;

            // The durations are printed with all their digits: 1000000, not 1e+06.
            std::cout << std::setprecision(std::numeric_limits<double>::digits10)
                      << "piook: carrier=" << params.transmissionFrequency << " Hz, type=" << typeName(params.type);
            if (params.type == MessageType::Symbols) {
                std::cout << ", symbol_time=" << params.symbolTime << " us";
            }
            std::cout << ", repeat=" << params.repeat << ", pause=" << params.pause
                      << " us, transmission_time=" << transmissionSamples * SAMPLE_TIME_US << " us" << std::endl;

            // Stop signals: end the transmission cleanly, also when the terminal closes.
            rpitx::realtime::installStopSignalHandlers(handleSignal);
            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            rpitx::clock::checkCarrierPll(params.transmissionFrequency, 0);
            RunLengthOokBurst burst{params.transmissionFrequency, runs};

            // The DMA engine times the whole chain by itself; wait for its end or for a stop request. A request
            // that came during the set-up leaves the chain unsent.
            if (running.load(std::memory_order_relaxed) != 0) {
                burst.start();
            }
            while (burst.isrunning() && running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(STOP_CHECK_INTERVAL);
            }
            // A stop request ends the chain where it is: the carrier GPIO may be keyed there.
            burst.stop();
            burst.disableclk(rpitx::dma::CARRIER_GPIO);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] piook: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "piook: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace piook

int main(int argc, char* argv[]) {
    return piook::run(argc, argv);
}
