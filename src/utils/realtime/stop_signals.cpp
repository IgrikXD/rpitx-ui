/**
 * @file stop_signals.cpp
 * @brief Stop signal handler installation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "stop_signals.h"

#include <signal.h>

#include <csignal>
#include <initializer_list>

void installStopSignalHandlers(void (*handler)(int)) noexcept {
    for (const int stopSignal: {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        std::signal(stopSignal, handler);
    }

    // Read the disposition of SIGHUP without changing it: nohup starts the process with SIGHUP ignored.
    struct sigaction hangup{};
    if (sigaction(SIGHUP, nullptr, &hangup) != 0 || hangup.sa_handler != SIG_IGN) {
        std::signal(SIGHUP, handler);
    }
}
