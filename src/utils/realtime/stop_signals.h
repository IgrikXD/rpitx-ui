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
 * @brief Install handler for the signals that ask a transmitter to stop: SIGTERM (rpitx-ui or a service
 *        manager), SIGINT (Ctrl+C), SIGQUIT (Ctrl+\), SIGPIPE (a write to a closed output pipe) and SIGHUP (a closed
 *        terminal or SSH session).
 *
 * The default action of each of them ends the process without the destructors that stop the DMA engine and the
 * clock generator. A SIGHUP ignored from the start, as nohup leaves it, stays ignored, so the transmitter outlives
 * the session. Transmitters call it right before their first librpitx object, so a stop signal at its default
 * action before that still ends the process at once, before anything is switched on.
 */
void installStopSignalHandlers(void (*handler)(int)) noexcept;
