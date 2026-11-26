/**
 * @file pipocsag.cpp
 * @brief POCSAG pager transmitter implementation.
 *
 * Calls a pager in the POCSAG code (ITU-R M.584-2, Radio-paging code No. 1): a
 * preamble and batches of codewords that carry the address of the pager and,
 * unless the call is tone-only, a numeric or an alphanumeric message. The bits
 * are sent as frequency shift keying at 512, 1200 or 2400 bit/s with the two
 * tones 4.5 kHz to either side of the requested frequency: a 0 above it and a
 * 1 below it, or the other way round with --invert. Transmission ends with the
 * last batch, or on a stop signal.
 *
 * @note Usage: pipocsag --freq <Hz> --address <address> [--message "<text>"] [--mode 512|1200|2400]
 *               [--type alphanumeric|numeric|tone-only] [--function a|b|c|d] [--invert] [-h | --help]
 *   - --freq      Carrier frequency in Hz, the centre of the two tones
 *   - --address   Pager address (RIC) in decimal notation
 *   - --message   Message to transmit (quote multi-word strings); none for a tone-only call
 *   - --mode      POCSAG mode, the bit rate in bit/s: 512 (default) | 1200 | 2400
 *   - --type      Message type: alphanumeric (default) | numeric | tone-only
 *   - --function  Function A - D of the pager: a (default) | b | c | d (default for an alphanumeric message)
 *   - --invert    Swap the two tones for a pager that decodes the inverted polarity
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 16.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pipocsag.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "realtime_scheduling.h"
#include "stop_signals.h"

namespace pipocsag {
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
         * @brief Parse a pager address written in decimal notation.
         *
         * Pager addresses are commonly written with leading zeros, which the integer
         * conversion of CLI11 would read as an octal prefix.
         *
         * @return Address, or std::nullopt if text is not a decimal number of 32 bits.
         */
        [[nodiscard]] std::optional<uint32_t> parseAddress(std::string_view text) noexcept {
            uint32_t address{};
            const auto last{text.data() + text.size()};
            if (const auto [ptr, ec]{std::from_chars(text.data(), last, address)}; ec != std::errc{} || ptr != last) {
                return std::nullopt;
            }
            return address;
        }

        /**
         * @brief Fill samples with the frequency samples of a word of the transmission, sent from bit 31 on.
         *
         * Every bit takes an equal share of the samples: a 1 is the tone at oneDeviation, a 0 the opposite one.
         */
        void modulateWord(uint32_t word, float oneDeviation, std::span<float> samples) {
            const auto bitSamples{static_cast<std::ptrdiff_t>(samples.size() / WORD_BITS)};
            auto sample{samples.begin()};
            for (int bit{WORD_BITS - 1}; bit >= 0; --bit) {
                sample = std::fill_n(sample, bitSamples, ((word >> bit) & 1U) != 0 ? oneDeviation : -oneDeviation);
            }
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PocsagParameters& params) {
        CLI::App app{"POCSAG pager transmitter (pager call -> librpitx ngfmdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz, the centre of the two tones")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        // Addresses a pager can have, as --help and the diagnostic name them.
        const auto frameAddresses{[](uint32_t first) {
            return std::to_string(first) + " - " + std::to_string(first | ((1U << FRAME_BITS) - 1U));
        }};
        const std::string addressRange{"0 - " + std::to_string(MAX_ADDRESS) + ", except " +
                                       frameAddresses(IDLE_ADDRESS) + " and " + frameAddresses(SYNC_ADDRESS) +
                                       " (the addresses of the idle and of the synchronization codeword)"};
        std::string addressText;
        app.add_option("--address", addressText, "Pager address (RIC) in decimal notation: " + addressRange)
            ->required();
        const auto* messageOption{app.add_option(
            "--message",
            params.message,
            "Message to transmit (quote multi-word strings, e.g. --message \"Hello world\"): 7-bit ASCII with "
            "--type alphanumeric, digits, space, U, - and the brackets [ ] or ( ) with --type numeric, none with "
            "--type tone-only")};

        // The --mode, --type and --function values are the names of their tables: IsMember replaces a
        // value with the spelling of the table, and its description is overridden because the option
        // descriptions already list the values.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const uint32_t bitRate: BIT_RATES) {
            modeNames.push_back(std::to_string(bitRate));
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back();
        }
        std::string mode{std::to_string(params.bitRate)};
        app.add_option("--mode", mode, "POCSAG mode, the bit rate in bit/s, " + mode + " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames).description(""));
        const std::vector<std::string> typeNames{TYPE_NAMES.begin(), TYPE_NAMES.end()};
        std::string type{typeName(params.type)};
        app.add_option("--type", type, "Message type: alphanumeric (default) | numeric | tone-only (no message)")
            ->transform(CLI::IsMember(typeNames, CLI::ignore_case).description(""));
        const std::vector<std::string> functionNames{FUNCTION_NAMES.begin(), FUNCTION_NAMES.end()};
        std::string function;
        const auto* functionOption{
            app.add_option("--function",
                           function,
                           "Function, the address A - D of the pager: a (default) | b | c | d (default with --type "
                           "alphanumeric)")
                ->transform(CLI::IsMember(functionNames, CLI::ignore_case).description(""))};
        app.add_flag("--invert",
                     params.invert,
                     "Swap the two tones (a 1 above the carrier, a 0 below it) for a pager that decodes the "
                     "inverted polarity");

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        const auto address{parseAddress(addressText)};
        if (address == std::nullopt || isPagerAddress(address.value()) == false) {
            std::cerr << "[ERROR] --address must be a pager address in decimal notation: " << addressRange << "!"
                      << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        params.address = address.value();

        // Position of a parsed name in its table.
        const auto position{[](const std::vector<std::string>& names, const std::string& name) {
            return static_cast<std::size_t>(std::ranges::find(names, name) - names.begin());
        }};
        params.bitRate = BIT_RATES[position(modeNames, mode)];
        params.type    = static_cast<MessageType>(position(typeNames, type));

        if (params.type == MessageType::ToneOnly) {
            // A tone-only call carries no message.
            if (messageOption->count() > 0) {
                std::cerr << "[ERROR] --message is not valid with --type tone-only!" << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
        } else if (params.message.empty()) {
            std::cerr << "[ERROR] --type " << typeName(params.type) << " requires a non-empty --message!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        } else if (isEncodable(params.type, params.message) == false) {
            std::cerr << "[ERROR] --message must contain only "
                      << (params.type == MessageType::Numeric ? "digits, spaces, U, - and brackets"
                                                              : "7-bit ASCII characters")
                      << " with --type " << typeName(params.type) << "!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.function = functionOption->count() == 0 ? defaultFunction(params.type)
                                                       : static_cast<Function>(position(functionNames, function));

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        PocsagParameters params;
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

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        rpitx::realtime::enableRealtimeScheduling();

        // A tone-only call carries no message.
        if (params.type != MessageType::ToneOnly) {
            std::cout << "pipocsag: message=\"" << params.message << "\"" << std::endl;
        }

        // Wrap the encoding, DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll refuses the PLL settings the board cannot run and
        // checkCarrierBand a carrier whose tones cross an integer step of the
        // PLL multiplier. A stray throw escaping main would terminate via
        // std::terminate without the destructor of ngfmdmasync stopping the
        // DMA engine and the clock generator.
        try {
            const auto words{encodeTransmission(params.address, params.function, params.type, params.message)};

            std::cout << "pipocsag: carrier=" << params.transmissionFrequency << " Hz, mode=" << params.bitRate
                      << " bit/s, type=" << typeName(params.type) << ", address=" << params.address
                      << ", function=" << functionName(params.function)
                      << ", polarity=" << (params.invert ? "inverted" : "normal")
                      << ", transmission_time=" << words.size() * WORD_BITS * 1000 / params.bitRate << " ms"
                      << std::endl;

            // Stop signals: end the transmission cleanly, also when the terminal closes.
            rpitx::realtime::installStopSignalHandlers(handleSignal);
            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            rpitx::clock::checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE};
            // Refuse the carriers at which the two tones lie on different sides of an integer step of the PLL
            // multiplier.
            rpitx::clock::checkCarrierBand(dma, params.transmissionFrequency, DEVIATION_HZ);
            // The idle samples around the transmission are the tone of a 1: a word of ones is a message
            // codeword, a word of zeros an address codeword.
            const float oneDeviation{params.invert ? DEVIATION_HZ : -DEVIATION_HZ};
            // Slot 0 and the start-up burst of the engine get idle samples, so the first sample reaches the air.
            rpitx::dma::startTransmission(dma, oneDeviation);

            // Queue the transmission in the DMA engine word by word, until a stop is requested.
            std::vector<float> samples(static_cast<std::size_t>(WORD_BITS) * (SAMPLE_RATE / params.bitRate));
            for (const uint32_t word: words) {
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                modulateWord(word, oneDeviation, samples);
                // SetFrequencySamples blocks until the samples are in the DMA FIFO (see
                // ngfmdmasync.cpp upstream), so no manual pacing is needed here.
                dma.SetFrequencySamples(samples.data(), samples.size());
            }

            // The queued words are transmitted when the transmission ended by itself, discarded on a stop request.
            const bool transmissionEnded{running.load(std::memory_order_relaxed) != 0};
            rpitx::dma::stopTransmission(dma, oneDeviation, transmissionEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pipocsag: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pipocsag: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pipocsag

int main(int argc, char* argv[]) {
    return pipocsag::run(argc, argv);
}
