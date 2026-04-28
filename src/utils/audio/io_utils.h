/**
 * @file io_utils.h
 * @brief Low-level POSIX I/O helpers shared across rpitx-ui tools.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>

/**
 * @brief Write all bytes from a buffer to a file descriptor.
 *
 * Wraps POSIX write() in a loop that handles partial writes (which can
 * happen on a pipe whose reader is slow). Returns false on the first
 * failed underlying write call.
 *
 * @param fd File descriptor to write to.
 * @param buf Pointer to the data buffer.
 * @param bytes Number of bytes to write.
 * @return true if all bytes were written, false on error.
 */
[[nodiscard]] bool writeAll(int fd, const void* buf, std::size_t bytes);
