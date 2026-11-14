/**
 * @file fake_clock.h
 * @brief Carrier clock stand-in for the unit tests of the clock utility library.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>

/**
 * @brief Carrier clock stand-in with the members the clock helpers use, named as librpitx names them.
 *
 * ComputeBestLO() records its arguments, leaves the configured divider behind and returns the
 * configured result, so a test states the outcome of librpitx instead of running it.
 * GetMasterFrac() rounds a deviation to the PLL fraction as clkgpio::GetMasterFrac does: the
 * multiplier of the carrier over the corrected crystal frequency is cut down to whole units of
 * the fraction, of which only the 20 bits of the fraction are kept.
 */
struct FakeClock {
    bool pi_is_2711{true};
    std::uint64_t XOSC_FREQUENCY{pi_is_2711 ? 54'000'000U : 19'200'000U};
    int PllFixDivider{1};
    double carrier{434'000'000.0};   ///< Unmodulated carrier in Hz (CentralFrequency of clkgpio).
    double clockCorrectionPpm{0.0};  ///< Clock correction as clkgpio takes it from NTP (clk_ppm).
    int result{0};                   ///< Value ComputeBestLO() returns.
    int divider{8};                  ///< Divider ComputeBestLO() leaves in PllFixDivider.
    std::uint64_t requestedFrequency{0};
    int requestedGuardBand{-1};

    int ComputeBestLO(std::uint64_t frequency, int guardBand) {
        requestedFrequency = frequency;
        requestedGuardBand = guardBand;
        PllFixDivider      = divider;
        return result;
    }

    std::uint32_t GetMasterFrac(double frequency) {
        const double crystal{static_cast<double>(XOSC_FREQUENCY) * (1.0 - clockCorrectionPpm * 1e-6)};
        double multiplier{0.0};
        if (PllFixDivider == 1) {
            // Pre-divider: carrier * 2 / crystal on BCM2711, carrier / (2 * crystal) on the earlier SoCs.
            multiplier = pi_is_2711 ? (carrier + frequency) * 2.0 / crystal : (carrier + frequency) / (2.0 * crystal);
        } else {
            multiplier = (carrier + frequency) * PllFixDivider * (pi_is_2711 ? 2.0 : 1.0) / crystal;
        }
        return static_cast<std::uint32_t>(multiplier * static_cast<double>(1U << 20U)) & 0xFFFFFU;
    }
};
