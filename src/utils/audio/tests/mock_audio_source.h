/**
 * @file mock_audio_source.h
 * @brief GoogleMock implementation of AudioSource for interaction-based unit tests.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <gmock/gmock.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "audio_source.h"

/**
 * @brief GoogleMock-based AudioSource used by the AudioPipeline interaction-tests.
 *
 * Complements FakeAudioSource: the fake supplies a real interleaved sample buffer for
 * tests that verify arithmetic (passthrough copy, downmix average, channel-preserve
 * propagation), while the mock targets the behavioural contracts that the fake cannot
 * express - per-call return sequences (e.g. positive read followed by error()=true),
 * call-count expectations on rewind() under loop mode, partial-frame returns that
 * trigger the SUT's std::logic_error branch, and StrictMock-driven assertions about
 * which source methods the ctor is allowed to touch.
 */
class MockAudioSource : public AudioSource {
public:
    MOCK_METHOD(AudioFormat, format, (), (const, override));
    MOCK_METHOD(std::string_view, description, (), (const, override));
    MOCK_METHOD(std::size_t, read, (std::span<float> dst), (override));
    MOCK_METHOD(bool, rewind, (), (override));
    MOCK_METHOD(bool, seekable, (), (const, override));
    MOCK_METHOD(bool, error, (), (const, override));
};
