/**
 * @file pipocsag.cpp
 * @brief POCSAG pager transmitter implementation.
 *
 * Calls a pager in the POCSAG code (ITU-R M.584-2, Radio-paging code No. 1): a
 * preamble and batches of codewords that carry the address of the pager and,
 * unless the call is tone-only, a numeric or an alphanumeric message. The bits
 * are sent as frequency shift keying at 512, 1200 or 2400 bit/s with the two
 * tones 4.5 kHz to either side of the requested frequency: a 0 above it and a
 * 1 below it. Transmission ends with the last batch, or on SIGTERM / SIGINT.
 *
 * @note Usage: pipocsag --freq <Hz> --address <address> [--message "<text>"] [--mode 512|1200|2400]
 *               [--type tone-only|numeric|alphanumeric] [--function a|b|c|d] [-h | --help]
 *   - --freq      Carrier frequency in Hz, the centre of the two tones
 *   - --address   Pager address (RIC) in decimal notation
 *   - --message   Message to transmit (quote multi-word strings); none for a tone-only call
 *   - --mode      POCSAG mode, the bit rate in bit/s: 512 | 1200 (default) | 2400
 *   - --type      Message type: tone-only | numeric | alphanumeric (default)
 *   - --function  Function A - D of the pager: a (default) | b | c | d (default for an alphanumeric message)
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
#include <csignal>
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
         * Every bit takes an equal share of the samples: a 1 is the tone below the carrier, a 0 the one above it.
         */
        void modulateWord(uint32_t word, std::span<float> samples) {
            const auto bitSamples{static_cast<std::ptrdiff_t>(samples.size() / WORD_BITS)};
            auto sample{samples.begin()};
            for (int bit{WORD_BITS - 1}; bit >= 0; --bit) {
                sample = std::fill_n(sample, bitSamples, ((word >> bit) & 1U) != 0 ? -DEVIATION_HZ : DEVIATION_HZ);
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
        const std::string addressRange{
            "0 - " + std::to_string(MAX_ADDRESS) + ", except " + std::to_string(IDLE_ADDRESS) + " - " +
            std::to_string(IDLE_ADDRESS | ((1U << FRAME_BITS) - 1U)) + " (the addresses of the idle codeword)"};
        std::string addressText;
        app.add_option("--address", addressText, "Pager address (RIC) in decimal notation: " + addressRange)
            ->required();
        const auto* messageOption{app.add_option(
            "--message",
            params.message,
            "Message to transmit (quote multi-word strings, e.g. --message \"Hello world\"): digits, space, U, - "
            "and the brackets [ ] or ( ) with --type numeric, 7-bit ASCII with --type alphanumeric, none with "
            "--type tone-only")};

        // The --mode values come from the bit rate table.
        std::string bitRateList;
        for (const uint32_t bitRate: BIT_RATES) {
            bitRateList += (bitRateList.empty() ? "" : " | ") + std::to_string(bitRate);
        }
        // The description of IsMember is overridden because the option description already lists the values.
        app.add_option(
               "--mode",
               params.bitRate,
               "POCSAG mode, the bit rate in bit/s, " + std::to_string(params.bitRate) + " by default: " + bitRateList)
            ->check(CLI::IsMember(BIT_RATES).description(""));

        // The --type and --function values come from their name tables: IsMember replaces the
        // value with the spelling of the table.
        const std::vector<std::string> typeNames{TYPE_NAMES.begin(), TYPE_NAMES.end()};
        std::string type{typeName(params.type)};
        app.add_option("--type", type, "Message type: tone-only (no message) | numeric | alphanumeric (default)")
            ->transform(CLI::IsMember(typeNames, CLI::ignore_case).description(""));
        const std::vector<std::string> functionNames{FUNCTION_NAMES.begin(), FUNCTION_NAMES.end()};
        std::string function;
        const auto* functionOption{
            app.add_option("--function",
                           function,
                           "Function, the address A - D of the pager: a (default) | b | c | d (default with --type "
                           "alphanumeric)")
                ->transform(CLI::IsMember(functionNames, CLI::ignore_case).description(""))};

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
        params.type    = static_cast<MessageType>(std::ranges::find(typeNames, type) - typeNames.begin());

        if (params.type == MessageType::ToneOnly) {
            // Reject (not ignore) a message the call cannot carry, so nothing but the requested call is sent.
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

        // Without --function, the function the Recommendation gives the messages of the format.
        params.function =
            functionOption->count() == 0
                ? defaultFunction(params.type)
                : static_cast<Function>(std::ranges::find(functionNames, function) - functionNames.begin());

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

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

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
                      << ", function=" << FUNCTION_NAMES[static_cast<std::size_t>(params.function)]
                      << ", transmission_time=" << words.size() * WORD_BITS * 1000 / params.bitRate << " ms"
                      << std::endl;

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE};
            // Refuse the carriers at which the two tones lie on different sides of an integer step of the PLL
            // multiplier.
            checkCarrierBand(dma, params.transmissionFrequency, DEVIATION_HZ);
            // Slot 0 and the start-up burst of the engine get idle samples, so the first sample reaches the air.
            startTransmission(dma, IDLE_DEVIATION_HZ);

            // Queue the transmission in the DMA engine word by word, until a stop is requested.
            std::vector<float> samples(static_cast<std::size_t>(WORD_BITS) * (SAMPLE_RATE / params.bitRate));
            for (const uint32_t word: words) {
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                modulateWord(word, samples);
                // SetFrequencySamples blocks until the samples are in the DMA FIFO (see
                // ngfmdmasync.cpp upstream), so no manual pacing is needed here.
                dma.SetFrequencySamples(samples.data(), samples.size());
            }

            // The queued words are transmitted when the transmission ended by itself, discarded on a stop request.
            const bool transmissionEnded{running.load(std::memory_order_relaxed) != 0};
            stopTransmission(dma, IDLE_DEVIATION_HZ, transmissionEnded);
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
