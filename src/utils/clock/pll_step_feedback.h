/**
 * @file pll_step_feedback.h
 * @brief Frequency deviations finer than the frequency step of the carrier PLL.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "carrier_pll.h"

/**
 * @brief Largest clock correction librpitx takes from NTP (clkgpio::SetppmFromNTP), as a part of the crystal frequency.
 */
inline constexpr double MAX_CLOCK_CORRECTION{200e-6};

/**
 * @brief Carries the part of a frequency sample the carrier PLL cannot express into the next sample.
 *
 * The FM engine of librpitx rounds every frequency sample down to the grid of
 * the PLL fraction (carrierStepHz()): a carrier step of 25.7 Hz above 375 MHz
 * on BCM2711 and of 36.6 Hz above 750 MHz on the earlier SoCs. The remainder
 * of each sample is added to the next one (first-order error feedback), so
 * the deviation averaged over a run of samples follows the requested one to a
 * fraction of the step. The deviations must not be negative: the engine is
 * never asked for less than the unmodulated carrier.
 *
 * @tparam Clock clkgpio, or any type with its pi_is_2711, XOSC_FREQUENCY, PllFixDivider and GetMasterFrac().
 *
 * @code
 * PllStepFeedback<clkgpio> stepFeedback{dma, 2'300.0F};
 * stepFeedback.apply(samples);
 * dma.SetFrequencySamples(samples.data(), samples.size());
 * @endcode
 */
template <typename Clock>
class PllStepFeedback {
public:
    /**
     * @brief Construct the feedback for the carrier clock of an FM engine.
     *
     * A request exceeds maxDeviation by less than one step and by the clock correction, which
     * carrierStepHz() leaves out; a second step is the reserve. The carrier is refused when the
     * range up to that request crosses an integer step of the PLL multiplier (crossesIntegerStep()).
     *
     * @param clock        Carrier clock, set up for its carrier.
     * @param maxDeviation Highest frequency deviation of the samples in Hz; the lowest one is 0.
     *
     * @throws std::runtime_error when a request would cross an integer step of the PLL multiplier.
     */
    PllStepFeedback(Clock& clock, float maxDeviation)
        : clock_{clock}, stepHz_{carrierStepHz(clock)}, idleFraction_{clock.GetMasterFrac(0.0)} {
        // Highest request: maxDeviation, the clock correction and two steps on top of it.
        const double maxRequest{static_cast<double>(maxDeviation) * (1.0 + MAX_CLOCK_CORRECTION) + 2.0 * stepHz_};
        if (crossesIntegerStep(clock, 0.0, maxRequest)) {
            throw std::runtime_error{"a frequency deviation of up to " + std::to_string(std::llround(maxDeviation)) +
                                     " Hz crosses an integer step of the PLL multiplier at this carrier and would "
                                     "wrap by a whole step; use another carrier frequency"};
        }
    }

    /**
     * @brief Replace every frequency sample by the deviation to request from the engine for it.
     * @param samples Frequency deviations in Hz, each within [0, maxDeviation].
     */
    void apply(std::span<float> samples) {
        for (float& sample: samples) {
            const double wanted{static_cast<double>(sample) + remainder_};
            sample = static_cast<float>(std::max(wanted, 0.0));
            const double fractionUnits{static_cast<double>(clock_.GetMasterFrac(sample)) -
                                       static_cast<double>(idleFraction_)};
            remainder_ = wanted - fractionUnits * stepHz_;
        }
    }

private:
    Clock& clock_;
    double stepHz_;               ///< Carrier frequency change of one unit of the PLL fraction.
    std::uint32_t idleFraction_;  ///< PLL fraction of the unmodulated carrier.
    double remainder_{0.0};       ///< Deviation requested so far that the engine has not delivered, in Hz.
};
