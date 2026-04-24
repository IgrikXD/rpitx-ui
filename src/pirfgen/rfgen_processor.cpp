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

#include <cassert>

#include "generators/multitone_generator.h"
#include "generators/noise_generator.h"
#include "generators/sweep_generator.h"

namespace {

    /**
     * @brief Pick the concrete RfGenerator subclass for a given config.
     *
     * All RfGenMode values are handled above; the trailing nullptr covers the
     * pathological case of an RfGenMode value outside the enum (e.g. invented by
     * an uninitialised or corrupted config) and is caught by the debug-build
     * assert in RfGenProcessor's constructor.
     *
     * @param config RF generator configuration parameters.
     * @return Unique pointer to the constructed generator, or nullptr on an unhandled mode.
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
        return nullptr;
    }

}  // namespace

RfGenProcessor::RfGenProcessor(RfGenConfig config) : generator_{makeGenerator(config)} {
    assert(generator_ != nullptr && "makeGenerator() returned nullptr - unhandled RfGenMode?");
}

RfGenProcessor::~RfGenProcessor() = default;

float RfGenProcessor::nextSample() {
    return generator_->nextSample();
}
