/**
 * @file wav_utils.cpp
 * @brief WAV header detection and I/O utility implementations.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "wav_utils.h"

#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace {
    // rpitx-ui only ever runs on Raspberry Pi (ARM Linux, little-endian) and
    // the WAV / RIFF format is itself little-endian, so the in-memory layout
    // of a fixed-width unsigned integer matches the on-disk byte order
    // verbatim. Verify the assumption at build time so the WAV parser stays
    // a plain memcpy rather than a byte-by-byte shift-OR pyramid - if the
    // toolchain ever targets a big-endian host this fails loudly here
    // instead of silently scrambling chunk sizes / sample rates.
    static_assert(std::endian::native == std::endian::little,
                  "rpitx-ui targets little-endian platforms only (Raspberry Pi).");

    /**
     * @brief Read a uint32_t from a 4-byte little-endian buffer.
     * @param p Source buffer (must be at least 4 bytes).
     * @return Parsed value.
     */
    [[nodiscard]] uint32_t readU32(const unsigned char* p) {
        uint32_t v{};
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    /**
     * @brief Read a uint16_t from a 2-byte little-endian buffer.
     * @param p Source buffer (must be at least 2 bytes).
     * @return Parsed value.
     */
    [[nodiscard]] uint16_t readU16(const unsigned char* p) {
        uint16_t v{};
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    /**
     * @brief Skip the given number of bytes from a stream, falling back to
     *        a read-and-discard loop when fseek fails (e.g. on a pipe).
     *
     * The two-tier strategy matters for stdin pipelines like
     * `cat file.wav | pifmrds ...`: pipes cannot seek, so we have to drain
     * the unwanted chunk by reading it. fseek is tried first because on a
     * real file (e.g. WAV opened directly) it is much faster than reading.
     *
     * @param input Stream to advance.
     * @param bytes Number of bytes to skip.
     * @return true on success, false on EOF / read error.
     */
    [[nodiscard]] bool skipBytes(std::FILE* input, uint32_t bytes) {
        if (std::fseek(input, static_cast<long>(bytes), SEEK_CUR) == 0) {
            return true;
        }
        unsigned char discard[256];
        while (bytes > 0) {
            const std::size_t toRead{std::min(static_cast<std::size_t>(bytes), sizeof(discard))};
            if (std::fread(discard, 1, toRead, input) != toRead) {
                return false;
            }
            bytes -= static_cast<uint32_t>(toRead);
        }
        return true;
    }
}  // namespace

std::optional<CarryBuffer> skipWavHeader(std::FILE* input) {
    // Read first 4 bytes to check for RIFF magic
    unsigned char hdr[4];
    if (std::fread(hdr, 1, 4, input) != 4) {
        return std::nullopt;
    }

    // Not a WAV file - treat bytes as raw PCM samples
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

    CarryBuffer info{};
    // Walk chunks until "data" is found, parsing "fmt " along the way so
    // the caller can validate the audio format before consuming samples.
    while (true) {
        unsigned char chunkHdr[8];
        if (std::fread(chunkHdr, 1, 8, input) != 8) {
            return std::nullopt;
        }

        uint32_t chunkSize{readU32(&chunkHdr[4])};

        if (std::memcmp(chunkHdr, "data", 4) == 0) {
            return info;
        }

        if (std::memcmp(chunkHdr, "fmt ", 4) == 0) {
            // PCM "fmt " is at least 16 bytes. We only need the first 16 -
            // anything beyond that is non-PCM extension data we don't care
            // about (we'd have to reject non-PCM at a higher level anyway,
            // since we read raw int16 samples downstream).
            constexpr uint32_t FMT_CHUNK_MIN{16};
            if (chunkSize < FMT_CHUNK_MIN) {
                return std::nullopt;
            }
            unsigned char fmt[FMT_CHUNK_MIN];
            if (std::fread(fmt, 1, FMT_CHUNK_MIN, input) != FMT_CHUNK_MIN) {
                return std::nullopt;
            }
            // fmt[0..1]   audio format (1 = PCM, others = compressed)
            // fmt[2..3]   number of channels
            // fmt[4..7]   sample rate (Hz)
            // fmt[8..11]  byte rate (sample rate * block align)
            // fmt[12..13] block align (channels * bits per sample / 8)
            // fmt[14..15] bits per sample
            info.channels      = readU16(&fmt[2]);
            info.sampleRate    = static_cast<int>(readU32(&fmt[4]));
            info.bitsPerSample = readU16(&fmt[14]);

            if (chunkSize > FMT_CHUNK_MIN) {
                if (skipBytes(input, chunkSize - FMT_CHUNK_MIN) == false) {
                    return std::nullopt;
                }
            }
            continue;
        }

        // Skip unknown chunk wholesale (LIST/INFO/JUNK/etc.).
        if (skipBytes(input, chunkSize) == false) {
            return std::nullopt;
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
