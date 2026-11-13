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
#include <stdexcept>
#include <string>
#include <string_view>

/**
 * @brief Highest carrier frequency librpitx has a PLL setting for on any board, in Hz (1 GHz on BCM2711).
 */
inline constexpr uint64_t MAX_CARRIER_FREQUENCY_HZ{2'000'000'000};

/**
 * @brief Minimum rate of the PLL that generates the carrier, in Hz.
 *
 * The BCM2835 / BCM2711 PLLs run from 600 MHz; librpitx accepts
 * dividers down to a 200 MHz PLL rate, and such a setting freezes the board.
 */
inline constexpr uint64_t MIN_PLL_RATE_HZ{600'000'000};

/**
 * @brief Largest divider librpitx chooses between the carrier PLL and the carrier.
 */
inline constexpr uint64_t MAX_PLL_DIVIDER{4095};

/**
 * @brief Rate of the carrier PLL for a divider chosen by librpitx, in Hz.
 *
 * Divider 1 stands for the feedback pre-divider of the PLL, which doubles its rate.
 *
 * @param pllFrequency Frequency the divider was chosen for: the carrier, twice the carrier on BCM2711.
 * @param divider      Divider chosen by clkgpio::ComputeBestLO (PllFixDivider).
 */
[[nodiscard]] constexpr uint64_t pllRateHz(uint64_t pllFrequency, int divider) noexcept {
    return pllFrequency * static_cast<uint64_t>(divider == 1 ? 2 : divider);
}

/**
 * @brief Number of units of the PLL fraction per whole step of the PLL multiplier (20 bits).
 */
inline constexpr double PLL_FRACTION_UNITS{1'048'576.0};

/**
 * @brief Carrier frequency change of one unit of the PLL fraction, in Hz.
 *
 * The DMA engines of librpitx modulate the carrier by rewriting the fraction of the PLL
 * multiplier (clkgpio::GetMasterFrac), so every frequency sample is rounded down to the
 * grid of this step. The step is the nominal one: the clock correction of librpitx,
 * below 200 parts per million, is left out.
 *
 * @tparam Clock clkgpio, or any type with its pi_is_2711, XOSC_FREQUENCY and PllFixDivider.
 * @param clock  Carrier clock whose PLL divider has been chosen.
 */
template <typename Clock>
[[nodiscard]] double carrierStepHz(const Clock& clock) noexcept {
    // PLL multiplier per Hz of the carrier, in units of 1 / XOSC_FREQUENCY.
    const double multiplierScale{clock.PllFixDivider == 1 ? (clock.pi_is_2711 ? 2.0 : 0.5)
                                                          : clock.PllFixDivider * (clock.pi_is_2711 ? 2.0 : 1.0)};
    return static_cast<double>(clock.XOSC_FREQUENCY) / (multiplierScale * PLL_FRACTION_UNITS);
}

/**
 * @brief Choose the PLL divider of clock for a carrier, refusing the settings the board cannot run.
 *
 * ComputeBestLO is called as clkgpio::SetCenterFrequency calls it: with the PLL frequency, twice
 * the carrier on BCM2711 (fixed post-divider), and the half-span guard band of librpitx.
 *
 * The diagnostic of a PLL rate below the minimum names its cause. A guard band at least as wide
 * as the distance between two integer steps of the PLL multiplier holds one for every divider that
 * keeps the PLL at its minimum rate, so only a higher carrier is accepted. A narrower guard band
 * holds one for every such divider only at some carriers, so another carrier nearby is accepted.
 *
 * @tparam Clock    clkgpio, or any type with its pi_is_2711, XOSC_FREQUENCY, PllFixDivider and ComputeBestLO().
 * @param clock     Carrier clock the divider is chosen on.
 * @param frequency Carrier frequency in Hz.
 * @param guardBand Guard band passed to ComputeBestLO, in Hz as librpitx counts it.
 * @param subject   What the setting does, named in the diagnostic (e.g. "generates a 50000 Hz carrier").
 * @param advice    What else to change, appended to the carrier advice of the diagnostic.
 *
 * @throws std::runtime_error when frequency exceeds MAX_CARRIER_FREQUENCY_HZ, ComputeBestLO finds no
 *         divider, or the chosen divider would run the PLL below MIN_PLL_RATE_HZ.
 */
template <typename Clock>
void choosePllDivider(Clock& clock, uint64_t frequency, int guardBand, std::string_view subject,
                      std::string_view advice) {
    if (frequency > MAX_CARRIER_FREQUENCY_HZ) {
        throw std::runtime_error{"no PLL setting generates a " + std::to_string(frequency) +
                                 " Hz carrier on any board (the limit is " + std::to_string(MAX_CARRIER_FREQUENCY_HZ) +
                                 " Hz)"};
    }
    const uint64_t pllFrequency{frequency * (clock.pi_is_2711 ? 2U : 1U)};
    if (clock.ComputeBestLO(pllFrequency, guardBand) != 0) {
        throw std::runtime_error{"no PLL setting generates a " + std::to_string(frequency) +
                                 " Hz carrier on this board"};
    }
    const uint64_t pllRate{pllRateHz(pllFrequency, clock.PllFixDivider)};
    if (pllRate < MIN_PLL_RATE_HZ) {
        // Smallest divider that keeps the PLL at its minimum rate: its integer steps lie the furthest apart.
        const uint64_t slowestDivider{(MIN_PLL_RATE_HZ + pllFrequency - 1) / pllFrequency};
        const bool nextToIntegerStep{slowestDivider <= MAX_PLL_DIVIDER &&
                                     2.0 * guardBand * static_cast<double>(slowestDivider) <
                                         static_cast<double>(clock.XOSC_FREQUENCY)};
        std::string message{"no PLL setting "};
        message += subject;
        message += " on this board (";
        message += nextToIntegerStep
                       ? "the carrier lies next to an integer step of the PLL multiplier for every divider that "
                         "runs the PLL at " +
                             std::to_string(MIN_PLL_RATE_HZ) + " Hz or above); use another carrier frequency"
                       : "the PLL would run at " + std::to_string(pllRate) + " Hz, below its " +
                             std::to_string(MIN_PLL_RATE_HZ) + " Hz minimum); use a higher carrier frequency";
        message += advice;
        throw std::runtime_error{message};
    }
}

/**
 * @brief Whether a range of frequency deviations around the carrier of clock crosses an integer step
 *        of the PLL multiplier.
 *
 * The DMA engines of librpitx rewrite only the fraction of the multiplier, so a frequency sample
 * behind an integer step is transmitted a whole step away from its frequency.
 *
 * @tparam Clock           clkgpio, or any type with its pi_is_2711, XOSC_FREQUENCY, PllFixDivider and
 *                         GetMasterFrac().
 * @param clock            Carrier clock set up for its carrier.
 * @param lowestDeviation  Lowest frequency deviation of the range, in Hz.
 * @param highestDeviation Highest frequency deviation of the range, in Hz.
 */
template <typename Clock>
[[nodiscard]] bool crossesIntegerStep(Clock& clock, double lowestDeviation, double highestDeviation) {
    // A range as wide as the distance between two integer steps holds one wherever it lies; in a
    // narrower one the fraction starts again from 0 behind the step.
    return highestDeviation - lowestDeviation >= carrierStepHz(clock) * PLL_FRACTION_UNITS ||
           clock.GetMasterFrac(lowestDeviation) > clock.GetMasterFrac(highestDeviation);
}

/**
 * @brief Refuse a carrier the DMA engines of librpitx would program although the board cannot run it.
 *
 * The constructors of iqdmasync, ngfmdmasync and amdmasync program the carrier PLL for
 * the band of the sample rate, and ookburst for the carrier alone, through
 * clkgpio::SetCenterFrequency, which ignores a failed divider choice; this check runs
 * the same choice on a clock of its own before the engine is constructed.
 *
 * @param frequency  Carrier frequency in Hz.
 * @param sampleRate Sample rate the engine is constructed with, in Hz (0 for ookburst).
 *
 * @throws std::runtime_error as choosePllDivider() does.
 */
void checkCarrierPll(uint64_t frequency, uint32_t sampleRate);

/**
 * @brief Refuse a carrier whose modulated band crosses an integer step of the PLL multiplier.
 *
 * librpitx keeps the band of the sample rate clear of the integer steps where a PLL divider
 * allows it and programs the carrier all the same where none does; the part of the band behind
 * the step would then be transmitted a whole step away (crossesIntegerStep()).
 *
 * @param clock         Carrier clock set up for its carrier: a clkgpio or a librpitx DMA engine.
 * @param frequency     Carrier frequency in Hz.
 * @param halfBandwidth Half the width of the band modulated around the carrier, in Hz: the highest
 *                      frequency deviation of the samples to either side of it.
 *
 * @throws std::runtime_error when the band crosses an integer step of the PLL multiplier.
 */
void checkCarrierBand(clkgpio& clock, uint64_t frequency, double halfBandwidth);

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
 * @throws std::runtime_error as choosePllDivider() and checkCarrierBand() do.
 */
void programCarrierPll(clkgpio& clock, uint64_t frequency, double halfBandwidth);
