/**
 * @file pissb.cpp
 * @brief Streaming SSB modulator implementation.
 *
 * Reads 16-bit PCM audio from stdin, applies SSB modulation (USB or LSB),
 * and writes float IQ pairs to stdout for consumption by sendiq.
 *
 * @note Usage: pissb [--sideband usb|lsb] [-h | --help]
 *   - --sideband  Sideband selection: usb (default) | lsb
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pissb.h"

#include <CLI/CLI.hpp>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "cli_common.h"
#include "io_utils.h"
#include "wav_utils.h"

namespace pissb {
    namespace {
        /**
         * @brief Atomic flag for graceful shutdown on signal reception.
         *
         * Must be lock-free to be safe to touch from a signal handler; statically
         * asserted below to fail fast on any exotic platform where it is not.
         */
        std::atomic<bool> running{true};
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free for signal-handler access");
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PissbParameters& params) {
        CLI::App app{"Streaming SSB modulator (stdin int16 PCM -> stdout float IQ)"};

        const std::map<std::string, SsbMode> sidebandMap{
            {"usb", SsbMode::USB},
            {"lsb", SsbMode::LSB},
        };
        app.add_option("--sideband", params.mode, "Sideband selection: usb (default) | lsb")
            ->transform(CLI::CheckedTransformer(sidebandMap, CLI::ignore_case));

        return rpitx::cli::parseCliApp(app, argc, argv);
    }

    int run(int argc, char* argv[]) {
        PissbParameters params;
        switch (parseArgs(argc, argv, params)) {
            case rpitx::cli::ParseResult::Ok:
                break;
            case rpitx::cli::ParseResult::Help:
                return 0;
            case rpitx::cli::ParseResult::Error:
                return 1;
        }

        std::signal(SIGTERM, handleSignal);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGPIPE, handleSignal);

        SsbProcessor ssb{params.mode};

        int16_t inbuf[BLOCK_SIZE];
        float outbuf[BLOCK_SIZE * 2];
        bool needHeader{true};

        while (running.load(std::memory_order_relaxed)) {
            // Detect and skip WAV header at stream boundaries
            if (needHeader) {
                auto result{skipWavHeader()};
                if (!result) {
                    break;
                }
                needHeader = false;

                // Process any carry-over samples from header detection
                for (int i{0}; i < result->count; ++i) {
                    const float sample{static_cast<float>(result->samples[i]) / PCM16_MAX};
                    const auto iq{ssb.process(sample)};
                    outbuf[i * 2]     = iq.i;
                    outbuf[i * 2 + 1] = iq.q;
                }
                if (result->count > 0) {
                    if (!writeAll(STDOUT_FILENO, outbuf, static_cast<size_t>(result->count) * 2 * sizeof(float))) {
                        break;
                    }
                }
            }

            // Read a block of PCM samples
            const auto n{static_cast<int>(std::fread(inbuf, sizeof(int16_t), BLOCK_SIZE, stdin))};
            if (n <= 0) {
                break;
            }

            // Convert and process each sample
            for (int i{0}; i < n; ++i) {
                const float sample{static_cast<float>(inbuf[i]) / PCM16_MAX};
                const auto iq{ssb.process(sample)};
                outbuf[i * 2]     = iq.i;
                outbuf[i * 2 + 1] = iq.q;
            }

            if (!writeAll(STDOUT_FILENO, outbuf, static_cast<size_t>(n) * 2 * sizeof(float))) {
                break;
            }

            // Partial read indicates end of WAV data - expect new header
            if (n < BLOCK_SIZE) {
                needHeader = true;
            }
        }

        return 0;
    }
}  // namespace pissb

int main(int argc, char* argv[]) {
    return pissb::run(argc, argv);
}
