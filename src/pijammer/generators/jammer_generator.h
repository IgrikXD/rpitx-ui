/**
 * @file jammer_generator.h
 * @brief Abstract base class for per-sample frequency-offset generators.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

/**
 * @brief Abstract base class for all jammer waveform generators.
 *
 * Defines the single-method contract consumed by JammerProcessor:
 * produce the next frequency offset in Hz on every call. Concrete
 * subclasses implement their own nextSample() with mode-specific 
 * state and logic.
 */
class JammerGenerator {
public:
    virtual ~JammerGenerator() = default;

    /**
     * @brief Advance state and produce the next frequency offset sample.
     * @return Frequency offset in Hz, within [-bandwidth/2, +bandwidth/2].
     */
    [[nodiscard]] virtual float nextSample() = 0;
};
