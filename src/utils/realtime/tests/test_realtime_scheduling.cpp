/**
 * @file test_realtime_scheduling.cpp
 * @brief Unit tests for the real-time scheduling switch.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>
#include <sched.h>

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "captured_streams_mixin.h"
#include "realtime_scheduling.h"

namespace {
    struct RealtimePolicyTestCase {
        std::string_view name;
        int policy;
        int priority;
    };

    /**
     * @brief Render a RealtimePolicyTestCase as its human-readable name and scheduling for gtest
     *        listings and failure messages.
     */
    void PrintTo(const RealtimePolicyTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {policy=" << testCase.policy << ", priority=" << testCase.priority << "}";
    }

    /**
     * @brief Build the real-time policies an operator may have chosen before the start: FIFO above
     *        the lowest priority and round-robin at the lowest one.
     */
    std::vector<RealtimePolicyTestCase> makeRealtimePolicyTestCases() {
        return {
            RealtimePolicyTestCase{"FifoAboveTheLowestPriority", SCHED_FIFO, sched_get_priority_min(SCHED_FIFO) + 1},
            RealtimePolicyTestCase{"RoundRobinAtTheLowestPriority", SCHED_RR, sched_get_priority_min(SCHED_RR)},
        };
    }
}  // namespace

/**
 * @brief Captures the diagnostics of each test, finds out whether the test process may use
 *        real-time scheduling at all, starts every test body on a normally scheduled thread,
 *        and restores the scheduling the test thread had before, so a real-time policy
 *        granted to one test does not leak into the next one.
 */
class EnableRealtimeSchedulingTest : public CapturedStreamsMixin<::testing::Test> {
protected:
    void SetUp() override {
        CapturedStreamsMixin::SetUp();
        savedPolicy_ = sched_getscheduler(0);
        ASSERT_NE(savedPolicy_, -1);
        ASSERT_EQ(sched_getparam(0, &savedParameters_), 0);

        const sched_param probeParameters{.sched_priority = sched_get_priority_min(SCHED_FIFO)};
        realtimePermitted_ = sched_setscheduler(0, SCHED_FIFO, &probeParameters) == 0;

        const sched_param normalParameters{.sched_priority = 0};
        ASSERT_EQ(sched_setscheduler(0, SCHED_OTHER, &normalParameters), 0);
    }

    void TearDown() override {
        EXPECT_EQ(sched_setscheduler(0, savedPolicy_, &savedParameters_), 0);
        CapturedStreamsMixin::TearDown();
    }

    /**
     * @brief Whether the kernel lets the test process use real-time scheduling.
     */
    bool realtimePermitted() const {
        return realtimePermitted_;
    }

private:
    int savedPolicy_{SCHED_OTHER};
    sched_param savedParameters_{};
    bool realtimePermitted_{false};
};

/**
 * @brief Where real-time scheduling is permitted a normally scheduled thread gets the FIFO
 *        policy at the lowest real-time priority; where it is not, the refusal is reported
 *        and the thread stays normally scheduled.
 *
 * Whether the kernel grants the request depends on the privileges of the test process, so
 * the fixture probes it and the test checks the complete outcome that belongs to the probed
 * case: the FIFO policy at the lowest real-time priority with an empty stderr, or the normal
 * policy at priority 0 together with the [WARNING] diagnostic on stderr. Stdout stays empty
 * in both cases.
 *
 * An implementation that never makes the request, makes one the kernel rejects, or asks for
 * another policy or priority fails where the request is granted; one that hides the refusal
 * or changes the policy or priority anyway fails where it is refused. Both captures are read
 * before the first expectation, so the message of a failed expectation reaches the console.
 */
TEST_F(EnableRealtimeSchedulingTest, GrantsLowestRealtimePriorityOrWarns) {
    enableRealtimeScheduling();

    const std::string& diagnostics{capturedStderr()};
    const std::string& standardOutput{capturedStdout()};
    const int currentPolicy{sched_getscheduler(0)};
    sched_param currentParameters{};
    ASSERT_EQ(sched_getparam(0, &currentParameters), 0);

    EXPECT_EQ(currentPolicy, realtimePermitted() ? SCHED_FIFO : SCHED_OTHER);
    EXPECT_EQ(currentParameters.sched_priority, realtimePermitted() ? sched_get_priority_min(SCHED_FIFO) : 0);
    EXPECT_EQ(diagnostics.empty(), realtimePermitted());
    EXPECT_EQ(diagnostics.starts_with("[WARNING]"), realtimePermitted() == false);
    EXPECT_TRUE(standardOutput.empty());
}

class EnableRealtimeSchedulingKeepsPolicyTest : public EnableRealtimeSchedulingTest,
                                                public ::testing::WithParamInterface<RealtimePolicyTestCase> {};

/**
 * @brief A thread that already runs under a real-time policy keeps its policy and priority.
 *
 * The operator may have started the transmitter through `chrt` or a systemd unit with a higher
 * priority than the helper would ask for, and the helper must not lower it. The test gives the
 * thread the scheduling of the case, calls the helper and expects the scheduling unchanged and
 * both streams empty. An implementation that requests FIFO at the lowest priority regardless
 * fails on the priority in the FIFO case and on the policy in the round-robin case. Where the
 * kernel does not permit real-time scheduling the case cannot be set up, so the test is skipped.
 */
TEST_P(EnableRealtimeSchedulingKeepsPolicyTest, KeepsAnExistingRealtimePolicy) {
    if (realtimePermitted() == false) {
        GTEST_SKIP() << "real-time scheduling is not permitted for this process";
    }
    const auto& testCase{GetParam()};
    const sched_param chosenParameters{.sched_priority = testCase.priority};
    ASSERT_EQ(sched_setscheduler(0, testCase.policy, &chosenParameters), 0);

    enableRealtimeScheduling();

    const std::string& diagnostics{capturedStderr()};
    const std::string& standardOutput{capturedStdout()};
    const int currentPolicy{sched_getscheduler(0)};
    sched_param currentParameters{};
    ASSERT_EQ(sched_getparam(0, &currentParameters), 0);

    EXPECT_EQ(currentPolicy, testCase.policy);
    EXPECT_EQ(currentParameters.sched_priority, testCase.priority);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_TRUE(standardOutput.empty());
}

INSTANTIATE_TEST_SUITE_P(ChosenBeforehand, EnableRealtimeSchedulingKeepsPolicyTest,
                         ::testing::ValuesIn(makeRealtimePolicyTestCases()),
                         [](const ::testing::TestParamInfo<RealtimePolicyTestCase>& info) {
                             return std::string{info.param.name};
                         });
