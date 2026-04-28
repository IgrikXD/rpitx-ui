/**
 * @file cli_validators.h
 * @brief Reusable CLI11 validators shared by migrated rpitx-ui binaries.
 *
 * Only validators that are genuinely shared across more than one migrated
 * binary live here. Module-specific rules (RDS PS/RT/PI lengths, the
 * pirfgen mode/tone-count relationship, the pichirp sweep period limits,
 * the pimorse message policy, the pissb sideband choice) intentionally
 * remain near their module so that opening the module file makes its
 * full CLI surface visible.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <CLI/CLI.hpp>

namespace rpitx::cli::validators {
    /**
     * @brief Validator that accepts only positive finite floating-point values.
     *
     * Rejects zero, negatives, NaN, and +-infinity. Intended for options
     * such as --bandwidth, --sweep-time, and --wpm where the downstream
     * algorithm cannot make progress with a non-positive or non-finite value.
     */
    extern const CLI::Validator PositiveFiniteFloat;

    /**
     * @brief Validator that accepts only textual frequency values that
     *        resolve to integer Hz.
     *
     * Backs --freq across all migrated transmitter binaries. Defers to
     * parseFrequencyHz so the accepted/rejected set stays in lock-step
     * with the post-parse conversion.
     */
    extern const CLI::Validator FrequencyHz;
}  // namespace rpitx::cli::validators
