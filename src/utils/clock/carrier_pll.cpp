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

void checkCarrierPll(uint64_t frequency, uint32_t sampleRate) {
    // A clock of its own: ComputeBestLO writes no register, SetPllNumber sets the source its destructor writes back.
    clkgpio clock;
    clock.SetPllNumber(clk_pllc, 0);
    std::string subject{"generates a " + std::to_string(frequency) + " Hz carrier"};
    if (sampleRate > 0) {
        subject += " at a " + std::to_string(sampleRate) + " Hz sample rate";
    }
    choosePllDivider(clock, frequency, static_cast<int>(sampleRate), subject, "");
}

void checkCarrierBand(clkgpio& clock, uint64_t frequency, double halfBandwidth) {
    if (crossesIntegerStep(clock, -halfBandwidth, halfBandwidth)) {
        throw std::runtime_error{"the " + std::to_string(std::llround(2.0 * halfBandwidth)) +
                                 " Hz band crosses an integer step of the PLL multiplier at a " +
                                 std::to_string(frequency) +
                                 " Hz carrier, part of it would wrap by a whole step; use another carrier frequency"};
    }
}

void programCarrierPll(clkgpio& clock, uint64_t frequency, double halfBandwidth) {
    const uint64_t pllFrequencyFactor{clock.pi_is_2711 ? 2U : 1U};
    const int guardBand{static_cast<int>(std::ceil(halfBandwidth * static_cast<double>(pllFrequencyFactor)))};
    const std::string bandwidth{std::to_string(std::llround(2.0 * halfBandwidth))};
    const std::string subject{halfBandwidth > 0.0
                                  ? "sweeps a " + bandwidth + " Hz band at " + std::to_string(frequency) + " Hz"
                                  : "generates a " + std::to_string(frequency) + " Hz carrier"};
    choosePllDivider(clock, frequency, guardBand, subject, halfBandwidth > 0.0 ? " or a smaller bandwidth" : "");
    clock.SetCenterFrequency(frequency, guardBand);
    // The band crosses an integer step when no divider keeps it between two of them.
    checkCarrierBand(clock, frequency, halfBandwidth);

    if ((clock.gpioreg[CM_LOCK] & CM_LOCK_FLOCKC) == 0) {
        std::cerr << "[WARNING] The carrier PLL reports no lock; the carrier may be off frequency." << std::endl;
    }
}
