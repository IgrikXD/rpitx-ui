/**
 * @file captured_streams_mixin.h
 * @brief Reusable gtest mixin that captures stdout and stderr for the duration of a test.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <gtest/gtest.h>

#include <string>

/**
 * @brief Test-mixin that arms gtest's stdout/stderr capturers in SetUp and exposes
 *        the captured text via lazy, cached accessors, releasing any unconsumed
 *        capture in TearDown.
 *
 * Parameterized by the gtest fixture base (`::testing::Test` for non-parametric
 * tests, `::testing::TestWithParam<T>` for value-parametrized ones) so the same
 * capture behaviour plugs into either kind of fixture without code duplication.
 * SetUp arms both captures unconditionally; the test body only pays the Get cost
 * for the streams it actually queries via capturedStdout() / capturedStderr().
 * The accessors cache on first call - gtest's API permits exactly one Get per
 * Capture, and the cache turns subsequent calls into a reference read instead of
 * a double-Get crash. TearDown releases any stream the body never consumed,
 * keeping the process-global capture state clean for the next test even when a
 * test bails out mid-body before reaching its stream assertions.
 */
template <typename Base>
class CapturedStreamsMixin : public Base {
protected:
    void SetUp() override {
        ::testing::internal::CaptureStdout();
        ::testing::internal::CaptureStderr();
    }

    void TearDown() override {
        if (stdoutConsumed_ == false) {
            ::testing::internal::GetCapturedStdout();
        }
        if (stderrConsumed_ == false) {
            ::testing::internal::GetCapturedStderr();
        }
    }

    const std::string& capturedStdout() {
        if (stdoutConsumed_ == false) {
            cachedStdout_   = ::testing::internal::GetCapturedStdout();
            stdoutConsumed_ = true;
        }
        return cachedStdout_;
    }

    const std::string& capturedStderr() {
        if (stderrConsumed_ == false) {
            cachedStderr_   = ::testing::internal::GetCapturedStderr();
            stderrConsumed_ = true;
        }
        return cachedStderr_;
    }

private:
    std::string cachedStdout_;
    std::string cachedStderr_;
    bool stdoutConsumed_{false};
    bool stderrConsumed_{false};
};
