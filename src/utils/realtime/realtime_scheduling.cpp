/**
 * @file realtime_scheduling.cpp
 * @brief Real-time scheduling implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "realtime_scheduling.h"

#include <sched.h>

#include <cerrno>
#include <iostream>
#include <system_error>

void enableRealtimeScheduling() {
    // Keep a real-time policy chosen before the start (chrt, systemd CPUSchedulingPolicy=).
    const int currentPolicy{sched_getscheduler(0) & ~SCHED_RESET_ON_FORK};
    if (currentPolicy == SCHED_FIFO || currentPolicy == SCHED_RR) {
        return;
    }

    const sched_param parameters{.sched_priority = sched_get_priority_min(SCHED_FIFO)};

    if (sched_setscheduler(0, SCHED_FIFO, &parameters) != 0) {
        const std::error_code requestError{errno, std::system_category()};
        std::cerr << "[WARNING] Real-time scheduling is unavailable (" << requestError.message()
                  << "); the transmission may glitch while other processes load the CPU." << std::endl;
    }
}
