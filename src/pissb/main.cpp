/**
 * @file main.cpp
 * @brief Entry point for pissb — streaming SSB modulator.
 *
 * Reads 16-bit PCM audio from stdin, applies SSB modulation (USB or LSB),
 * and writes float IQ pairs to stdout for consumption by sendiq.
 *
 * @note Usage: pissb [-u | -l]
 *   - -u  Upper sideband (default)
 *   - -l  Lower sideband
 */

#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "ssb_processor.h"
#include "wav_utils.h"

/**
 * @brief Block size for PCM sample processing (~21 ms at 48 kHz).
 */
static constexpr int BLOCK_SIZE{1024};

/**
 * @brief Normalization divisor for int16_t -> float [-1.0, 1.0] conversion (2^15).
 */
static constexpr float PCM16_MAX{static_cast<float>(std::numeric_limits<int16_t>::max()) + 1.0f};

/**
 * @brief Atomic flag for graceful shutdown on signal reception.
 */
static volatile sig_atomic_t running{1};

/**
 * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
 * @param sig Signal number (unused).
 */
static void handleSignal(int /*sig*/) {
    running = 0;
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    SsbMode mode{SsbMode::USB};
    if (argc > 1 && std::strcmp(argv[1], "-l") == 0) {
        mode = SsbMode::LSB;
    }

    std::signal(SIGTERM, handleSignal);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGPIPE, handleSignal);

    SsbProcessor ssb{mode};

    int16_t inbuf[BLOCK_SIZE];
    float outbuf[BLOCK_SIZE * 2];
    bool needHeader{true};

    while (running) {
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

        // Partial read indicates end of WAV data — expect new header
        if (n < BLOCK_SIZE) {
            needHeader = true;
        }
    }

    return 0;
}
