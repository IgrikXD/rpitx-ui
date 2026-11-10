/**
 * @file carrier_pll.cpp
 * @brief Carrier PLL setup implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "carrier_pll.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    /**
     * @brief Choose the PLL divider of clock for the carrier, refusing the settings the board cannot run.
     *
     * clkgpio::SetCenterFrequency runs ComputeBestLO with a half-span guard band on the PLL
     * frequency, which is twice the carrier on BCM2711 (fixed post-divider); ComputeBestLO only
     * selects the divider and fails outside the carrier range of the PLL (keeping the previous
     * divider), and for a low carrier its dividers run the PLL below the minimum - either setting
     * freezes the board, so the refusal comes before the PLL is programmed.
     *
     * @param guardBand Guard band passed to ComputeBestLO, as librpitx counts it.
     * @param subject   What the carrier setting does, named in the diagnostic ("generates a ... carrier").
     * @param advice    What else to change, appended to "use a higher carrier frequency".
     */
    void choosePllDivider(clkgpio& clock, uint64_t frequency, int guardBand, std::string_view subject,
                          std::string_view advice) {
        if (frequency > MAX_CARRIER_FREQUENCY_HZ) {
            std::ostringstream message;
            message << "no PLL setting generates a " << frequency << " Hz carrier on any board (the limit is "
                    << MAX_CARRIER_FREQUENCY_HZ << " Hz)";
            throw std::runtime_error{message.str()};
        }
        const uint64_t pllFrequency{frequency * (clock.pi_is_2711 ? 2U : 1U)};
        if (clock.ComputeBestLO(pllFrequency, guardBand) != 0) {
            std::ostringstream message;
            message << "no PLL setting generates a " << frequency << " Hz carrier on this board";
            throw std::runtime_error{message.str()};
        }
        const double pllRate{pllRateHz(pllFrequency, clock.PllFixDivider)};
        if (pllRate < MIN_PLL_RATE_HZ) {
            std::ostringstream message;
            message << "no PLL setting " << subject << " on this board (the PLL would run at " << pllRate / 1e6
                    << " MHz, below its " << MIN_PLL_RATE_HZ / 1e6 << " MHz minimum); use a higher carrier frequency"
                    << advice;
            throw std::runtime_error{message.str()};
        }
    }
}  // namespace

void checkCarrierPll(uint64_t frequency, uint32_t sampleRate) {
    // A clock of its own, set up as the engine will set it up; ComputeBestLO writes no register.
    clkgpio clock;
    clock.SetAdvancedPllMode(true);
    std::ostringstream subject;
    subject << "generates a " << frequency << " Hz carrier at a " << sampleRate << " Hz sample rate";
    choosePllDivider(clock, frequency, static_cast<int>(sampleRate), subject.str(), "");
}

void programCarrierPll(clkgpio& clock, uint64_t frequency, double halfBandwidth) {
    const uint64_t pllFrequencyFactor{clock.pi_is_2711 ? 2U : 1U};
    const int guardBand{static_cast<int>(std::ceil(halfBandwidth * static_cast<double>(pllFrequencyFactor)))};
    const long long bandwidth{std::llround(2.0 * halfBandwidth)};
    std::ostringstream subject;
    if (halfBandwidth > 0.0) {
        subject << "sweeps a " << bandwidth << " Hz band at " << frequency << " Hz";
    } else {
        subject << "generates a " << frequency << " Hz carrier";
    }
    choosePllDivider(clock, frequency, guardBand, subject.str(), halfBandwidth > 0.0 ? " or a smaller bandwidth" : "");
    clock.SetCenterFrequency(frequency, guardBand);

    // The modulation writes only the 20-bit fractional part of the PLL multiplier: when no divider
    // keeps the band between two integer steps, the fraction wraps across the band and so would the
    // modulation.
    if (halfBandwidth > 0.0 && clock.GetMasterFrac(-halfBandwidth) > clock.GetMasterFrac(halfBandwidth)) {
        std::ostringstream message;
        message << "the " << bandwidth << " Hz band crosses an integer step of the PLL multiplier at a " << frequency
                << " Hz carrier, part of it would wrap by a whole step; use a smaller bandwidth or "
                   "another carrier frequency";
        throw std::runtime_error{message.str()};
    }

    if ((clock.gpioreg[CM_LOCK] & CM_LOCK_FLOCKC) == 0) {
        std::cerr << "[WARNING] The carrier PLL reports no lock; the carrier may be off frequency." << std::endl;
    }
}
