/**
 * @file io_utils.cpp
 * @brief POSIX I/O helper implementations.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "io_utils.h"

#include <unistd.h>

bool writeAll(int fd, const void* buf, std::size_t bytes) {
    auto ptr{static_cast<const char*>(buf)};

    while (bytes > 0) {
        const ssize_t written{write(fd, ptr, bytes)};
        if (written <= 0) {
            return false;
        }
        ptr += written;
        bytes -= static_cast<std::size_t>(written);
    }

    return true;
}
