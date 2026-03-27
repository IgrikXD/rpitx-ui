/**
 * @file wav_utils.cpp
 * @brief WAV header detection and I/O utility implementations.
 */

#include "wav_utils.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>

std::optional<CarryBuffer> skipWavHeader(std::FILE* input) {
    // Read first 4 bytes to check for RIFF magic
    unsigned char hdr[4];
    if (std::fread(hdr, 1, 4, input) != 4) {
        return std::nullopt;
    }

    // Not a WAV file — treat bytes as raw PCM samples
    if (std::memcmp(hdr, "RIFF", 4) != 0) {
        CarryBuffer carry{};
        std::memcpy(carry.samples.data(), hdr, 4);
        carry.count = 2;
        return carry;
    }

    // Skip file size (4 bytes) + "WAVE" tag (4 bytes)
    unsigned char skip[8];
    if (std::fread(skip, 1, 8, input) != 8) {
        return std::nullopt;
    }

    // Walk chunks until "data" is found
    while (true) {
        unsigned char chunkHdr[8];
        if (std::fread(chunkHdr, 1, 8, input) != 8) {
            return std::nullopt;
        }

        auto chunkSize{static_cast<uint32_t>(chunkHdr[4]) | (static_cast<uint32_t>(chunkHdr[5]) << 8) |
                       (static_cast<uint32_t>(chunkHdr[6]) << 16) | (static_cast<uint32_t>(chunkHdr[7]) << 24)};

        if (std::memcmp(chunkHdr, "data", 4) == 0) {
            return CarryBuffer{};
        }

        // Skip non-data chunk (try seek first, fall back to read-discard)
        if (std::fseek(input, static_cast<long>(chunkSize), SEEK_CUR) != 0) {
            unsigned char discard[256];
            while (chunkSize > 0) {
                const size_t toRead{std::min(static_cast<size_t>(chunkSize), sizeof(discard))};
                if (std::fread(discard, 1, toRead, input) != toRead) {
                    return std::nullopt;
                }
                chunkSize -= static_cast<uint32_t>(toRead);
            }
        }
    }
}

bool writeAll(int fd, const void* buf, size_t bytes) {
    auto ptr{static_cast<const char*>(buf)};

    while (bytes > 0) {
        const ssize_t written{write(fd, ptr, bytes)};
        if (written <= 0) {
            return false;
        }
        ptr += written;
        bytes -= static_cast<size_t>(written);
    }

    return true;
}
