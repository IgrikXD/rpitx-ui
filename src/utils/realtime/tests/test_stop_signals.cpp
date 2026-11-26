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
    using rpitx::realtime::installStopSignalHandlers;

    /**
     * @brief Handler of a signal as sigaction reports it.
     */
    using SignalHandler = void (*)(int);

    /**
     * @brief Signals installStopSignalHandlers handles.
     */
    constexpr std::array<int, 5> STOP_SIGNALS{SIGTERM, SIGINT, SIGQUIT, SIGPIPE, SIGHUP};

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
     * @brief Every stop signal at its default action and ignored from the start.
     */
    std::vector<StopSignalTestCase> makeStopSignalTestCases() {
        return {
            StopSignalTestCase{"Terminate", SIGTERM, false, true},
            StopSignalTestCase{"Interrupt", SIGINT, false, true},
            StopSignalTestCase{"Quit", SIGQUIT, false, true},
            StopSignalTestCase{"BrokenPipe", SIGPIPE, false, true},
            StopSignalTestCase{"Hangup", SIGHUP, false, true},
            StopSignalTestCase{"IgnoredTerminate", SIGTERM, true, true},
            StopSignalTestCase{"IgnoredInterrupt", SIGINT, true, true},
            StopSignalTestCase{"IgnoredQuit", SIGQUIT, true, true},
            StopSignalTestCase{"IgnoredBrokenPipe", SIGPIPE, true, true},
            StopSignalTestCase{"IgnoredHangup", SIGHUP, true, false},
        };
    }
}  // namespace

/**
 * @brief Starts every test with the stop signals and SIGWINCH at their default action and restores them afterwards.
 */
class InstallStopSignalHandlersTest : public ::testing::TestWithParam<StopSignalTestCase> {
protected:
    void SetUp() override {
        for (std::size_t index{0}; index < STOP_SIGNALS.size(); ++index) {
            ASSERT_EQ(sigaction(STOP_SIGNALS[index], nullptr, &savedDispositions_[index]), 0);
            std::signal(STOP_SIGNALS[index], SIG_DFL);
        }
        ASSERT_EQ(sigaction(SIGWINCH, nullptr, &savedResizeDisposition_), 0);
        std::signal(SIGWINCH, SIG_DFL);
        receivedSignal = 0;
    }

    void TearDown() override {
        for (std::size_t index{0}; index < STOP_SIGNALS.size(); ++index) {
            EXPECT_EQ(sigaction(STOP_SIGNALS[index], &savedDispositions_[index], nullptr), 0);
        }
        EXPECT_EQ(sigaction(SIGWINCH, &savedResizeDisposition_, nullptr), 0);
    }

private:
    std::array<struct sigaction, STOP_SIGNALS.size()> savedDispositions_{};
    struct sigaction savedResizeDisposition_{};
};

/**
 * @brief installStopSignalHandlers catches every stop signal, also one ignored from the start, except a SIGHUP
 *        ignored from the start, which stays ignored as nohup left it.
 *
 * Every disposition, SIGWINCH at its default action included, is checked before the raise, so a missed signal fails the
 * test instead of ending the test process; a caught signal must reach the test handler and keep it installed.
 */
TEST_P(InstallStopSignalHandlersTest, CatchesTheSignalUnlessNohupIgnoresTheHangup) {
    const auto& testCase{GetParam()};
    std::signal(testCase.signal, testCase.ignoredAtStart ? SIG_IGN : SIG_DFL);

    installStopSignalHandlers(recordSignal);

    for (const int stopSignal: STOP_SIGNALS) {
        const bool keptIgnored{stopSignal == testCase.signal && testCase.caught == false};
        ASSERT_EQ(currentHandler(stopSignal), keptIgnored ? SIG_IGN : recordSignal) << "signal " << stopSignal;
    }
    EXPECT_EQ(currentHandler(SIGWINCH), SIG_DFL) << "signal " << SIGWINCH;
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
