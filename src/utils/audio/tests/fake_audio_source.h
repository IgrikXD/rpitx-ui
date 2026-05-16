/**
 * @file fake_audio_source.h
 * @brief Programmable in-memory AudioSource for unit-test fixtures.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio_source.h"

/**
 * @brief In-memory sample-buffer AudioSource for tests that need real samples flowing through
 *        the AudioPipeline data path.
 *
 * Narrowly scoped to the data-flow side of the test suite: passthrough bit-exactness, downmix
 * averaging, channel-preserve propagation, loop replay continuity, and frame-count / resample
 * checks. Every behaviour-side test (empty source, sticky error, partial-frame return, rewind
 * interaction, ctor-touches-nothing-else) uses MockAudioSource instead, where each per-method
 * return value is a literal in the test body rather than a slice of state-machine logic in
 * this header - which keeps the bug surface for test infrastructure at zero.
 */
class FakeAudioSource final : public AudioSource {
public:
    FakeAudioSource(AudioFormat format, std::vector<float> samples, bool seekable)
        : format_{format}, samples_{std::move(samples)}, seekable_{seekable} {
    }

    [[nodiscard]] AudioFormat format() const override {
        return format_;
    }

    [[nodiscard]] std::string_view description() const override {
        return "fake";
    }

    [[nodiscard]] std::size_t read(std::span<float> dst) override {
        const auto channels{static_cast<std::size_t>(format_.channels)};
        // Mirror the throw-on-misalignment contract enforced by
        // LibsndfileAudioSource::read so a misuse of the fake by a future test surfaces as a
        // loud programmer error rather than as a confusing short read.
        if (channels == 0 || dst.empty() || dst.size() % channels != 0) {
            throw std::invalid_argument{"FakeAudioSource::read: dst must be a non-empty whole number of frames (size " +
                                        std::to_string(dst.size()) + ", channels " + std::to_string(channels) + ")"};
        }
        const std::size_t available{samples_.size() - readPos_};
        const std::size_t take{std::min(available, dst.size())};
        std::copy_n(samples_.data() + readPos_, take, dst.begin());
        readPos_ += take;
        return take;
    }

    [[nodiscard]] bool rewind() override {
        if (seekable_ == false) {
            return false;
        }
        readPos_ = 0;
        return true;
    }

    [[nodiscard]] bool seekable() const override {
        return seekable_;
    }
    [[nodiscard]] bool error() const override {
        return false;
    }

private:
    AudioFormat format_;
    std::vector<float> samples_;
    bool seekable_;
    std::size_t readPos_{0};
};
