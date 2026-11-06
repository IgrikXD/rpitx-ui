/**
 * @file flush_to_zero.h
 * @brief Flush-to-zero floating-point mode for the DSP thread.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <fpu_control.h>

/**
 * @brief Enable flush-to-zero mode for floating-point arithmetic on the calling thread.
 *
 * Subnormal operands and results are replaced by zero, so a decaying filter
 * state settles to silence instead of lingering in the subnormal range.
 */
inline void enableFlushToZero() noexcept {
    // FZ bit, at the same position in the AArch32 FPSCR and the AArch64 FPCR.
    constexpr fpu_control_t FLUSH_TO_ZERO_BIT{1U << 24};

    fpu_control_t controlWord{};
    _FPU_GETCW(controlWord);
    _FPU_SETCW(controlWord | FLUSH_TO_ZERO_BIT);
}
