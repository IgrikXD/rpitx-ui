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
#include <cstring>
#include <iostream>

void enableRealtimeScheduling() {
    const sched_param parameters{.sched_priority = sched_get_priority_min(SCHED_FIFO)};

    if (sched_setscheduler(0, SCHED_FIFO, &parameters) != 0) {
        std::cerr << "[WARNING] Real-time scheduling is unavailable (" << std::strerror(errno)
                  << "); the transmission may glitch while other processes load the CPU." << std::endl;
    }
}
