/**
 * @file pirtty.cpp
 * @brief Radioteletype (RTTY) transmitter implementation.
 *
 * Transmits a text message in the Baudot code (ITA2) as frequency shift keying
 * with a 170 Hz shift at 45.45, 50 or 75 baud: a start bit, five data bits and
 * 1.5 stop bits per character. The space and mark tones are transmitted as the
 * carrier shifted above the requested frequency, so a receiver tuned to it in
 * USB mode hears them at 1000 Hz and 1170 Hz; the mark is the higher frequency,
 * as in amateur RTTY. Transmission ends with the message, or on SIGTERM / SIGINT.
 *
 * @note Usage: pirtty --freq <Hz> --message "<text>" [--mode 45|50|75] [-h | --help]
 *   - --freq      Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode
 *   - --message   Message to transmit in the ITA2 alphabet (quote multi-word strings); letters are sent in upper case
 *   - --mode      RTTY mode: 45 (45.45 baud, default) | 50 (50 baud) | 75 (75 baud)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pirtty.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "baudot_encoder.h"
#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "pll_step_feedback.h"
#include "realtime_scheduling.h"

namespace pirtty {
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
         * @brief Append the frequency samples of a Baudot code to samples.
         *
         * The start bit is a space, the data bits follow from bit 0 on (mark for a 1,
         * space for a 0) and the stop bit is a mark.
         *
         * @param code       Five-bit Baudot code.
         * @param bitSamples Number of samples of one bit.
         * @param samples    Destination.
         */
        void appendCharacter(std::uint8_t code, std::size_t bitSamples, std::vector<float>& samples) {
            const auto stopSamples{static_cast<std::size_t>(STOP_BITS * static_cast<double>(bitSamples))};
            samples.insert(samples.end(), bitSamples, SPACE_TONE_HZ);
            for (int bit{0}; bit < BAUDOT_CODE_BITS; ++bit) {
                samples.insert(samples.end(), bitSamples, ((code >> bit) & 1U) != 0 ? MARK_TONE_HZ : SPACE_TONE_HZ);
            }
            samples.insert(samples.end(), stopSamples, MARK_TONE_HZ);
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], RttyParameters& params) {
        CLI::App app{"Radioteletype transmitter (text message -> librpitx ngfmdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq",
                       transmissionFrequencyText,
                       "Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--message",
                       params.message,
                       "Message to transmit (quote multi-word strings, e.g. --message \"CQ CQ DE RPITX\") in the ITA2 "
                       "alphabet: Latin letters (sent in upper case), digits, space, CR, LF, BELL and "
                       "! \" # $ & ' ( ) , - . / : ; ?")
            ->required();

        // The --mode values come from the mode table.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const RttyMode& mode: RTTY_MODES) {
            modeNames.emplace_back(mode.name);
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back();
        }
        std::string modeName{params.mode->name};
        // The description of IsMember is overridden because the option description already lists the values.
        app.add_option("--mode",
                       modeName,
                       "RTTY mode, the baud rate with a 170 Hz shift (45 stands for 45.45), " + modeName +
                           " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames).description(""));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (params.message.empty()) {
            std::cerr << "[ERROR] --message must not be empty!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (std::ranges::any_of(params.message, [](char ch) { return charToBaudot(ch) == std::nullopt; })) {
            std::cerr << "[ERROR] --message must contain only characters of the ITA2 alphabet!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.mode = &*std::ranges::find(RTTY_MODES, modeName, &RttyMode::name);
        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        RttyParameters params;
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
        const RttyMode& mode{*params.mode};

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

        std::cout << "pirtty: message=\"" << params.message << "\"" << std::endl;

        const auto codes{encodeMessage(params.message)};
        const std::size_t bitSamples{samplesPerBit(mode)};
        // A character is the start bit, the data bits and the stop bit.
        const double transmissionSeconds{static_cast<double>(codes.size()) * (1.0 + BAUDOT_CODE_BITS + STOP_BITS) /
                                         mode.baudRate};

        // Wrap DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll refuses the PLL settings the board cannot run and
        // PllStepFeedback a carrier whose tones cross an integer step of the
        // PLL multiplier. A stray throw escaping main would terminate via
        // std::terminate without the destructor of ngfmdmasync stopping the
        // DMA engine and the clock generator.
        try {
            std::cout << "pirtty: carrier=" << params.transmissionFrequency << " Hz, mode=" << mode.name << " ("
                      << mode.baudRate << " baud), space=" << SPACE_TONE_HZ << " Hz, mark=" << MARK_TONE_HZ
                      << " Hz, transmission_time=" << std::lround(transmissionSeconds) << " s" << std::endl;

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE};
            // The engine rounds every sample to the frequency step of the PLL; the rounding is carried into the next.
            PllStepFeedback<clkgpio> stepFeedback{dma, MARK_TONE_HZ};
            // Slot 0 and the start-up burst of the engine get the idle mark, so the first sample reaches the air.
            startTransmission(dma, MARK_TONE_HZ);

            // Queue the characters in the DMA engine block by block, until a stop is requested.
            std::vector<float> samples;
            for (const std::uint8_t code: codes) {
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                samples.clear();
                appendCharacter(code, bitSamples, samples);
                for (std::size_t offset{0}; offset < samples.size() && running.load(std::memory_order_relaxed);
                     offset += DMA_BLOCK_SAMPLES) {
                    const auto block{
                        std::span{samples}.subspan(offset, std::min(DMA_BLOCK_SAMPLES, samples.size() - offset))};
                    stepFeedback.apply(block);
                    // SetFrequencySamples blocks until the block is in the DMA FIFO (see
                    // ngfmdmasync.cpp upstream), so no manual pacing is needed here.
                    dma.SetFrequencySamples(block.data(), block.size());
                }
            }

            // The queued characters are transmitted when the message ended, discarded on a stop request.
            const bool messageEnded{running.load(std::memory_order_relaxed) != 0};
            stopTransmission(dma, MARK_TONE_HZ, messageEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pirtty: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pirtty: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pirtty

int main(int argc, char* argv[]) {
    return pirtty::run(argc, argv);
}
