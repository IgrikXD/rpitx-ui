/**
 * @file realtime_scheduling.h
 * @brief Real-time scheduling for the thread that feeds the DMA buffer.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

/**
 * @brief Give the calling thread real-time FIFO scheduling at the lowest real-time priority.
 *
 * A real-time thread runs ahead of every normally scheduled process, so the DMA
 * buffer is still fed in time while other processes compete for the CPU. A thread
 * that already has a real-time policy (FIFO or round-robin, e.g. started through
 * `chrt` or a systemd unit) keeps its policy and priority. Prints a warning to
 * stderr when the kernel refuses the request.
 */
void enableRealtimeScheduling();
