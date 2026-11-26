/**
 * @file test_stop_signals.cpp
 * @brief Unit tests for the installation of the stop signal handlers.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>
#include <signal.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "stop_signals.h"

namespace {
    /**
     * @brief Handler of a signal as sigaction reports it.
     */
    using SignalHandler = void (*)(int);

    /**
     * @brief Signals installStopSignalHandlers handles.
     */
    constexpr std::array<int, 4> STOP_SIGNALS{SIGTERM, SIGINT, SIGPIPE, SIGHUP};

    /**
     * @brief Last signal the test handler received, 0 for none.
     */
    volatile std::sig_atomic_t receivedSignal{0};

    /**
     * @brief Test handler: records the signal it received.
     */
    void recordSignal(int sig) {
        receivedSignal = sig;
    }

    /**
     * @brief Current handler of sig, read without changing it.
     */
    SignalHandler currentHandler(int sig) {
        struct sigaction current{};
        sigaction(sig, nullptr, &current);
        return current.sa_handler;
    }

    struct StopSignalTestCase {
        std::string_view name;
        int signal;
        bool ignoredAtStart;
        bool caught;
    };

    /**
     * @brief Render a StopSignalTestCase as its name, signal and dispositions for gtest listings and
     *        failure messages.
     */
    void PrintTo(const StopSignalTestCase& testCase, std::ostream* os) {
        *os << std::boolalpha << testCase.name << " {signal=" << testCase.signal
            << ", ignoredAtStart=" << testCase.ignoredAtStart << ", caught=" << testCase.caught << "}";
    }

    /**
     * @brief Every stop signal at its default action and ignored from the start: a background job of a
     *        non-interactive shell starts with SIGINT ignored, a program started by nohup with SIGHUP ignored.
     */
    std::vector<StopSignalTestCase> makeStopSignalTestCases() {
        return {
            StopSignalTestCase{"Terminate", SIGTERM, false, true},
            StopSignalTestCase{"Interrupt", SIGINT, false, true},
            StopSignalTestCase{"BrokenPipe", SIGPIPE, false, true},
            StopSignalTestCase{"Hangup", SIGHUP, false, true},
            StopSignalTestCase{"IgnoredTerminate", SIGTERM, true, true},
            StopSignalTestCase{"IgnoredInterrupt", SIGINT, true, true},
            StopSignalTestCase{"IgnoredBrokenPipe", SIGPIPE, true, true},
            StopSignalTestCase{"IgnoredHangup", SIGHUP, true, false},
        };
    }
}  // namespace

/**
 * @brief Saves the dispositions of the stop signals, starts every test with them at their default action and
 *        an empty record of the test handler, and restores the saved dispositions afterwards, so a handler
 *        installed by one test does not leak into the next one.
 */
class InstallStopSignalHandlersTest : public ::testing::TestWithParam<StopSignalTestCase> {
protected:
    void SetUp() override {
        for (std::size_t index{0}; index < STOP_SIGNALS.size(); ++index) {
            ASSERT_EQ(sigaction(STOP_SIGNALS[index], nullptr, &savedDispositions_[index]), 0);
            std::signal(STOP_SIGNALS[index], SIG_DFL);
        }
        receivedSignal = 0;
    }

    void TearDown() override {
        for (std::size_t index{0}; index < STOP_SIGNALS.size(); ++index) {
            EXPECT_EQ(sigaction(STOP_SIGNALS[index], &savedDispositions_[index], nullptr), 0);
        }
    }

private:
    std::array<struct sigaction, STOP_SIGNALS.size()> savedDispositions_{};
};

/**
 * @brief installStopSignalHandlers catches every stop signal, also one ignored from the start, except a SIGHUP
 *        ignored from the start, which stays ignored as nohup left it.
 *
 * The test starts the signal of the case at its default action or ignored and the other stop signals at their
 * default action, installs the handlers, and checks every disposition before it raises the signal: a caught
 * signal must reach the test handler and leave it installed for the next one, as a closed terminal may send
 * SIGHUP more than once; an ignored hangup must leave the record empty and the process running. An
 * implementation that misses a signal fails on its disposition before the raise could end the test process; one
 * that catches an ignored SIGHUP fails the IgnoredHangup case, one that keeps every ignored signal fails the other
 * ignored cases, one that skips the other signals once it finds SIGHUP ignored fails on their dispositions, and
 * one that resets a handler after its first signal fails after the raise.
 */
TEST_P(InstallStopSignalHandlersTest, CatchesTheSignalUnlessNohupIgnoresTheHangup) {
    const auto& testCase{GetParam()};
    std::signal(testCase.signal, testCase.ignoredAtStart ? SIG_IGN : SIG_DFL);

    installStopSignalHandlers(recordSignal);

    for (const int stopSignal: STOP_SIGNALS) {
        const bool keptIgnored{stopSignal == testCase.signal && testCase.caught == false};
        ASSERT_EQ(currentHandler(stopSignal), keptIgnored ? SIG_IGN : recordSignal) << "signal " << stopSignal;
    }
    ASSERT_EQ(std::raise(testCase.signal), 0);
    const int received{receivedSignal};

    EXPECT_EQ(received, testCase.caught ? testCase.signal : 0);
    EXPECT_EQ(currentHandler(testCase.signal), testCase.caught ? recordSignal : SIG_IGN);
}

INSTANTIATE_TEST_SUITE_P(DispositionsAtStart, InstallStopSignalHandlersTest,
                         ::testing::ValuesIn(makeStopSignalTestCases()),
                         [](const ::testing::TestParamInfo<StopSignalTestCase>& info) {
                             return std::string{info.param.name};
                         });
