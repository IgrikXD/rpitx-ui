/**
 * @file carrier_pll.h
 * @brief Setup of the carrier PLL of librpitx with the settings the board cannot run refused.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <librpitx/librpitx.h>

#include <cstdint>

/**
 * @brief Highest carrier frequency librpitx has a PLL setting for on any board, in Hz.
 *
 * The limit of a BCM2711 is 1 GHz, found at run time; far above this one the
 * range check of librpitx overflows instead of failing.
 */
inline constexpr uint64_t MAX_CARRIER_FREQUENCY_HZ{2'000'000'000};

/**
 * @brief Minimum rate of the PLL that generates the carrier, in Hz.
 *
 * The BCM2835 / BCM2711 PLLs run from 600 MHz; librpitx accepts
 * dividers down to a 200 MHz PLL rate, and such a setting freezes the board.
 */
inline constexpr double MIN_PLL_RATE_HZ{600e6};

/**
 * @brief Rate of the carrier PLL for a divider chosen by librpitx, in Hz.
 *
 * Divider 1 stands for the feedback pre-divider of the PLL, which doubles its rate.
 *
 * @param pllFrequency Frequency the divider was chosen for: the carrier, twice the carrier on BCM2711.
 * @param divider      Divider chosen by clkgpio::ComputeBestLO (PllFixDivider).
 */
[[nodiscard]] constexpr double pllRateHz(uint64_t pllFrequency, int divider) noexcept {
    return static_cast<double>(pllFrequency) * static_cast<double>(divider == 1 ? 2 : divider);
}

/**
 * @brief Program the carrier PLL of clock for frequency and the band of +-halfBandwidth around it.
 *
 * The PLL divider is chosen for the whole band and the PLL is programmed through
 * clkgpio::SetCenterFrequency, which also starts the clock generator; the carrier
 * GPIO is left as it is. A warning is printed when the PLL reports no lock.
 *
 * @param clock         Carrier clock in advanced PLL mode: a clkgpio or a librpitx DMA engine.
 * @param frequency     Carrier frequency in Hz.
 * @param halfBandwidth Half the width of the band modulated around the carrier, in Hz (0 for an
 *                      unmodulated carrier).
 *
 * @throws std::runtime_error when frequency exceeds MAX_CARRIER_FREQUENCY_HZ, librpitx finds no PLL
 *         setting for the carrier, the setting would run the PLL below MIN_PLL_RATE_HZ, or the band
 *         crosses an integer step of the PLL multiplier.
 */
void programCarrierPll(clkgpio& clock, uint64_t frequency, double halfBandwidth);
