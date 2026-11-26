/**
 * @file stop_signals.h
 * @brief Installation of the stop signal handler of a transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

/**
 * @brief Install the handler for SIGTERM, SIGINT, SIGQUIT, SIGPIPE and SIGHUP, which ask a transmitter to stop.
 *
 * Their default action ends the process without the destructors that stop the DMA engine and the clock generator.
 * A SIGHUP ignored from the start, as nohup leaves it, stays ignored.
 */
void installStopSignalHandlers(void (*handler)(int)) noexcept;
