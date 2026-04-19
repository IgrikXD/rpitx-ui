/**
 * @file rfgen_processor.h
 * @brief Frequency-offset generator for RF generator modes.
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
class RfGenerator;

/**
 * @brief RF generator waveform selection mode.
 */
enum class RfGenMode : uint8_t {
    Noise,      ///< Uniform pseudo-random frequency offset within the bandwidth.
    Sweep,      ///< Fast sawtooth sweep across the bandwidth.
    Multitone,  ///< Random fast-hopping across N equidistant tones (FHSS-style).
};

/**
 * @brief Configuration parameters for the RfGenProcessor.
 *
 * @attention All fields must be set explicitly - no in-class initializers.
 *
 * @code
 * RfGenProcessor rfgen{{.mode = RfGenMode::Multitone, .bandwidth = 50'000.0F,
 *                       .sampleRate = 200'000, .toneCount = 8}};
 * @endcode
 */
struct RfGenConfig {
    RfGenMode mode;       ///< Active RF generator mode.
    float bandwidth;      ///< Generated bandwidth in Hz (full width, symmetric around center).
    uint32_t sampleRate;  ///< DMA sample rate in Hz.
    int toneCount;        ///< Number of tones for multitone mode (ignored otherwise).
};

/**
 * @brief Per-sample frequency-offset generator for wideband RF generation.
 *
 * Produces a stream of frequency offsets in Hz (relative to the carrier)
 * suitable for direct consumption by ngfmdmasync::SetFrequencySample().
 * Internally delegates to a mode-specific RfGenerator subclass, chosen at
 * construction time based on the configured RfGenMode.
 *
 * @code
 * RfGenProcessor rfgen{config};
 * const float offset{rfgen.nextSample()};
 * @endcode
 */
class RfGenProcessor {
public:
    /**
     * @brief Construct an RfGenProcessor for the given configuration.
     * @param config RF generator configuration parameters.
     */
    explicit RfGenProcessor(RfGenConfig config);

    RfGenProcessor(const RfGenProcessor&)            = delete;
    RfGenProcessor& operator=(const RfGenProcessor&) = delete;
    RfGenProcessor(RfGenProcessor&&)                 = delete;
    RfGenProcessor& operator=(RfGenProcessor&&)      = delete;

    /**
     * @brief Defined out-of-line (= default in the .cpp) so that std::unique_ptr's
     * deleter is instantiated where RfGenerator is complete - a forward
     * declaration alone would fail std::default_delete's sizeof(T) check.
     */
    ~RfGenProcessor();

    /**
     * @brief Advance state and produce the next frequency offset sample.
     * @return Frequency offset in Hz, within [-bandwidth/2, +bandwidth/2].
     */
    [[nodiscard]] float nextSample();

private:
    /**
     * @brief Polymorphic generator instance for the active RF generator mode.
     */
    std::unique_ptr<RfGenerator> generator_;
};
