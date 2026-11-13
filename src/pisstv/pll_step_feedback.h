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

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "carrier_pll.h"

namespace pisstv {
    /**
     * @brief Carries the part of a frequency sample the carrier PLL cannot express into the next sample.
     *
     * The DMA engine rounds every frequency deviation down to a whole unit of
     * the PLL fraction: a carrier step of 25.7 Hz above 375 MHz on BCM2711 and
     * of 36.6 Hz above 750 MHz on the earlier SoCs, which leaves 31 or 22 levels
     * between black and white. The remainder of each sample is added to the next
     * one (first-order error feedback), so the deviation averaged over a tone
     * follows the requested one to a fraction of the step.
     *
     * @tparam Clock clkgpio, or any type with its pi_is_2711, XOSC_FREQUENCY, PllFixDivider and GetMasterFrac().
     *
     * @code
     * PllStepFeedback<clkgpio> feedback{dma, 2'300.0F};
     * feedback.apply(samples);
     * dma.SetFrequencySamples(samples.data(), samples.size());
     * @endcode
     */
    template <typename Clock>
    class PllStepFeedback {
    public:
        /**
         * @brief Construct the feedback for the carrier clock of a DMA engine.
         * @param clock        Carrier clock, set up for its carrier.
         * @param maxDeviation Highest frequency deviation of the samples in Hz; the lowest one is 0.
         *
         * @throws std::runtime_error when the deviations cross an integer step of the PLL multiplier:
         *         the engine rewrites only the fraction, so the carrier would wrap by a whole step.
         */
        PllStepFeedback(Clock& clock, float maxDeviation)
            : clock_{clock}, stepHz_{carrierStepHz(clock)}, idleFraction_{clock.GetMasterFrac(0.0)} {
            if (clock.GetMasterFrac(-stepHz_) > idleFraction_ ||
                clock.GetMasterFrac(static_cast<double>(maxDeviation) + stepHz_) < idleFraction_) {
                throw std::runtime_error{"a frequency deviation of up to " +
                                         std::to_string(static_cast<int>(maxDeviation)) +
                                         " Hz crosses an integer step of the PLL multiplier at this carrier and "
                                         "would wrap by a whole step; use another carrier frequency"};
            }
        }

        /**
         * @brief Replace every frequency sample by the deviation to request from the engine for it.
         */
        void apply(std::span<float> samples) {
            for (float& sample: samples) {
                const double wanted{static_cast<double>(sample) + remainder_};
                sample = static_cast<float>(wanted);
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
}  // namespace pisstv
