/**
 * @file jammer_processor.cpp
 * @brief JammerProcessor implementation - owns and delegates to a JammerGenerator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "jammer_processor.h"

#include <cassert>

#include "generators/multitone_generator.h"
#include "generators/noise_generator.h"
#include "generators/sweep_generator.h"

namespace {

    /**
     * @brief Pick the concrete JammerGenerator subclass for a given config.
     *
     * All JammerMode values are handled above; the trailing nullptr covers the
     * pathological case of a JammerMode value outside the enum (e.g. invented by
     * an uninitialised or corrupted config) and is caught by the debug-build
     * assert in JammerProcessor's constructor.
     *
     * @param config Jammer configuration parameters.
     * @return Unique pointer to the constructed generator, or nullptr on an unhandled mode.
     */
    [[nodiscard]] std::unique_ptr<JammerGenerator> makeGenerator(const JammerConfig& config) {
        switch (config.mode) {
            case JammerMode::Noise:
                return std::make_unique<NoiseGenerator>(config.bandwidth, config.sampleRate);
            case JammerMode::Sweep:
                return std::make_unique<SweepGenerator>(config.bandwidth, config.sampleRate);
            case JammerMode::Multitone:
                return std::make_unique<MultitoneGenerator>(config.bandwidth, config.sampleRate, config.toneCount);
        }
        return nullptr;
    }

}  // namespace

JammerProcessor::JammerProcessor(JammerConfig config) : generator_{makeGenerator(config)} {
    assert(generator_ != nullptr && "makeGenerator() returned nullptr - unhandled JammerMode?");
}

JammerProcessor::~JammerProcessor() = default;

float JammerProcessor::nextSample() {
    return generator_->nextSample();
}
