/**
 * @file wav_utils.h
 * @brief WAV file header detection and low-level I/O utilities.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

/**
 * @brief Buffer for non-WAV bytes read during header detection.
 *
 * When the input does not start with a RIFF header, the first 4 bytes
 * are reinterpreted as two int16_t PCM samples and stored here.
 */
struct CarryBuffer {
    std::array<int16_t, 2> samples{};  ///< Carry-over PCM samples.
    int count{};                       ///< Number of valid samples (0 or 2).
};

/**
 * @brief Attempt to detect and skip a WAV (RIFF) header.
 *
 * Reads from @p input. If a RIFF header is found, navigates to the "data"
 * chunk and positions the stream right after it. If the stream does not
 * start with "RIFF", the 4 bytes read are stored in the returned CarryBuffer.
 *
 * @param input File stream to read from (defaults to stdin).
 * @return CarryBuffer on success, std::nullopt on read error / EOF.
 */
[[nodiscard]] std::optional<CarryBuffer> skipWavHeader(std::FILE* input = stdin);

/**
 * @brief Write all bytes from a buffer to a file descriptor.
 *
 * Retries partial writes until all data is written or an error occurs.
 *
 * @param fd File descriptor to write to.
 * @param buf Pointer to the data buffer.
 * @param bytes Number of bytes to write.
 * @return true if all bytes were written, false on error.
 */
[[nodiscard]] bool writeAll(int fd, const void* buf, size_t bytes);
