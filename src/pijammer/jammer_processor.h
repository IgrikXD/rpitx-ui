/**
 * @file jammer_processor.h
 * @brief Frequency-offset generator for RF jamming modes.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <memory>

// Forward declaration to avoid including all the generator headers in this public interface.
class JammerGenerator;

/**
 * @brief Jammer waveform selection mode.
 */
enum class JammerMode : uint8_t {
    Noise,      ///< Uniform pseudo-random frequency offset within the bandwidth.
    Sweep,      ///< Fast sawtooth sweep across the bandwidth.
    Multitone,  ///< Random fast-hopping across N equidistant tones (FHSS-style).
};

/**
 * @brief Configuration parameters for the JammerProcessor.
 *
 * @attention All fields must be set explicitly - no in-class initializers.
 *
 * @code
 * JammerProcessor jammer{{.mode = JammerMode::Multitone, .bandwidth = 50'000.0F,
 *                         .sampleRate = 200'000, .toneCount = 8}};
 * @endcode
 */
struct JammerConfig {
    JammerMode mode;      ///< Active jamming mode.
    float bandwidth;      ///< Jamming bandwidth in Hz (full width, symmetric around center).
    uint32_t sampleRate;  ///< DMA sample rate in Hz.
    int toneCount;        ///< Number of tones for multitone mode (ignored otherwise).
};

/**
 * @brief Per-sample frequency-offset generator for jamming.
 *
 * Produces a stream of frequency offsets in Hz (relative to the carrier)
 * suitable for direct consumption by ngfmdmasync::SetFrequencySample().
 * Internally delegates to a mode-specific JammerGenerator subclass, chosen at
 * construction time based on the configured JammerMode.
 *
 * @code
 * JammerProcessor jammer{config};
 * const float offset{jammer.nextSample()};
 * @endcode
 */
class JammerProcessor {
public:
    /**
     * @brief Construct a JammerProcessor for the given configuration.
     * @param config Jammer configuration parameters.
     */
    explicit JammerProcessor(JammerConfig config);

    JammerProcessor(const JammerProcessor&)            = delete;
    JammerProcessor& operator=(const JammerProcessor&) = delete;
    JammerProcessor(JammerProcessor&&)                 = delete;
    JammerProcessor& operator=(JammerProcessor&&)      = delete;

    /**
     * @brief Defined out-of-line (= default in the .cpp) so that std::unique_ptr's
     * deleter is instantiated where JammerGenerator is complete - a forward
     * declaration alone would fail std::default_delete's sizeof(T) check.
     */
    ~JammerProcessor();

    /**
     * @brief Advance state and produce the next frequency offset sample.
     * @return Frequency offset in Hz, within [-bandwidth/2, +bandwidth/2].
     */
    [[nodiscard]] float nextSample();

private:
    /**
     * @brief Polymorphic generator instance for the active jamming mode.
     */
    std::unique_ptr<JammerGenerator> generator_;
};
