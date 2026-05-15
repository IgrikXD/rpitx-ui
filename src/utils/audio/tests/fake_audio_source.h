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
 * @brief Lightweight programmable AudioSource for the AudioPipeline tests.
 *
 * Backed by an in-memory float vector. Tests configure the channel count, sample rate, and
 * seekability up-front, then drive the pipeline through the public read() / rewind() path
 * without spinning up a libsndfile-backed file source. setError() drives the pipeline's
 * source-error branch.
 */
class FakeAudioSource final : public AudioSource {
public:
    FakeAudioSource(AudioFormat format, std::vector<float> samples, bool seekable)
        : format_{format}, samples_{std::move(samples)}, seekable_{seekable} {
    }

    /**
     * @brief Construct a no-data source (read() returns 0 immediately).
     *
     * Useful when only the format / seekable bits are exercised, e.g. in validateLoopSupport
     * tests where the pipeline never actually reads the source.
     */
    FakeAudioSource(AudioFormat format, bool seekable) : format_{format}, seekable_{seekable} {
    }

    /**
     * @brief Set the sticky error flag. Once true, read() returns 0 and error() returns true
     *        on every subsequent call, matching the AudioSource contract.
     */
    void setError(bool flag) {
        error_ = flag;
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
        // LibsndfileAudioSource::read so the fake stays substitutable in any
        // test that drives the AudioSource contract directly instead of
        // through AudioPipeline.
        if (channels == 0 || dst.empty() || dst.size() % channels != 0) {
            throw std::invalid_argument{
                "FakeAudioSource::read: dst must be a non-empty whole number of frames (size " +
                std::to_string(dst.size()) + ", channels " + std::to_string(channels) + ")"};
        }
        if (error_) {
            return 0;
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
        return error_;
    }

private:
    AudioFormat format_;
    std::vector<float> samples_;
    bool seekable_;
    bool error_{false};
    std::size_t readPos_{0};
};
