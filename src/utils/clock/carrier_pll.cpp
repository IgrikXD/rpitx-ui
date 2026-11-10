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

void programCarrierPll(clkgpio& clock, uint64_t frequency, double halfBandwidth) {
    if (frequency > MAX_CARRIER_FREQUENCY_HZ) {
        std::ostringstream message;
        message << "no PLL setting generates a " << frequency << " Hz carrier on any board (the limit is "
                << MAX_CARRIER_FREQUENCY_HZ << " Hz)";
        throw std::runtime_error{message.str()};
    }

    // clkgpio::SetCenterFrequency runs ComputeBestLO with a half-span guard band on the PLL
    // frequency, which is twice the carrier on BCM2711 (fixed post-divider), so the same
    // arguments are prepared here to check the divider before the PLL is programmed.
    const uint64_t pllFrequencyFactor{clock.pi_is_2711 ? 2U : 1U};
    const uint64_t pllFrequency{frequency * pllFrequencyFactor};
    const int guardBand{static_cast<int>(std::ceil(halfBandwidth * static_cast<double>(pllFrequencyFactor)))};

    // ComputeBestLO only selects the divider; it fails outside the carrier range of the PLL (and keeps
    // the previous divider), and a wide band at a low carrier can only be swept with a small divider,
    // whose PLL rate lies below the minimum - either setting freezes the board, so refuse before
    // programming it.
    if (clock.ComputeBestLO(pllFrequency, guardBand) != 0) {
        std::ostringstream message;
        message << "no PLL setting generates a " << frequency << " Hz carrier on this board";
        throw std::runtime_error{message.str()};
    }
    const double pllRate{pllRateHz(pllFrequency, clock.PllFixDivider)};
    const long long bandwidth{std::llround(2.0 * halfBandwidth)};
    if (pllRate < MIN_PLL_RATE_HZ) {
        std::ostringstream message;
        if (halfBandwidth > 0.0) {
            message << "no PLL setting sweeps a " << bandwidth << " Hz band at " << frequency << " Hz";
        } else {
            message << "no PLL setting generates a " << frequency << " Hz carrier";
        }
        message << " on this board (the PLL would run at " << pllRate / 1e6 << " MHz, below its "
                << MIN_PLL_RATE_HZ / 1e6 << " MHz minimum); use a higher carrier frequency"
                << (halfBandwidth > 0.0 ? " or a smaller bandwidth" : "");
        throw std::runtime_error{message.str()};
    }
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
