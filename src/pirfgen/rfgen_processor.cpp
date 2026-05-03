/**
 * @file rfgen_processor.cpp
 * @brief RfGenProcessor implementation - owns and delegates to an RfGenerator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "rfgen_processor.h"

#include "generators/multitone_generator.h"
#include "generators/noise_generator.h"
#include "generators/sweep_generator.h"

namespace {

    /**
     * @brief Pick the concrete RfGenerator subclass for a given config.
     *
     * The switch covers every RfGenMode value; -Wswitch-enum guards against a
     * future enumerator slipping through. The trailing
     * __builtin_unreachable() collapses the post-switch path so the compiler
     * does not synthesise an extra return (which would otherwise emit a
     * runtime nullptr branch reachable only via UB - corrupted memory or a
     * cast from outside the enum domain).
     *
     * @param config RF generator configuration parameters.
     * @return Unique pointer to the constructed generator (never nullptr).
     */
    [[nodiscard]] std::unique_ptr<RfGenerator> makeGenerator(const RfGenConfig& config) {
        switch (config.mode) {
            case RfGenMode::Noise:
                return std::make_unique<NoiseGenerator>(config.bandwidth, config.sampleRate);
            case RfGenMode::Sweep:
                return std::make_unique<SweepGenerator>(config.bandwidth, config.sampleRate);
            case RfGenMode::Multitone:
                return std::make_unique<MultitoneGenerator>(config.bandwidth, config.sampleRate, config.toneCount);
        }
        __builtin_unreachable();
    }

}  // namespace

RfGenProcessor::RfGenProcessor(RfGenConfig config) : generator_{makeGenerator(config)} {
}

RfGenProcessor::~RfGenProcessor() = default;

float RfGenProcessor::nextSample() {
    return generator_->nextSample();
}
