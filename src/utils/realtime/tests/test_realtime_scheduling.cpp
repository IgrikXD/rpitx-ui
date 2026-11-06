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

#include <string>

#include "captured_streams_mixin.h"
#include "realtime_scheduling.h"

/**
 * @brief Captures the diagnostics of each test, finds out whether the test process may use
 *        real-time scheduling at all, and restores the scheduling of the test thread
 *        afterwards, so a real-time policy granted to one test does not leak into the next one.
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
        ASSERT_EQ(sched_setscheduler(0, savedPolicy_, &savedParameters_), 0);
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

    /**
     * @brief Scheduling policy the test thread had before the test body ran.
     */
    int savedPolicy() const {
        return savedPolicy_;
    }

    /**
     * @brief Priority the test thread had before the test body ran.
     */
    int savedPriority() const {
        return savedParameters_.sched_priority;
    }

private:
    int savedPolicy_{SCHED_OTHER};
    sched_param savedParameters_{};
    bool realtimePermitted_{false};
};

/**
 * @brief Where real-time scheduling is permitted the thread gets the FIFO policy at the
 *        lowest real-time priority; where it is not, the refusal is reported and the
 *        scheduling stays as it was.
 *
 * Whether the kernel grants the request depends on the privileges of the test process, so
 * the fixture probes it and the test checks the complete outcome that belongs to the probed
 * case: the FIFO policy at the lowest real-time priority with an empty stderr, or the saved
 * policy and priority together with the [WARNING] diagnostic on stderr. Stdout stays empty
 * in both cases. The lowest priority already runs ahead of every normally scheduled process
 * and keeps the transmitter behind the real-time threads of the system itself.
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

    EXPECT_EQ(currentPolicy, realtimePermitted() ? SCHED_FIFO : savedPolicy());
    EXPECT_EQ(currentParameters.sched_priority,
              realtimePermitted() ? sched_get_priority_min(SCHED_FIFO) : savedPriority());
    EXPECT_EQ(diagnostics.empty(), realtimePermitted());
    EXPECT_EQ(diagnostics.starts_with("[WARNING]"), realtimePermitted() == false);
    EXPECT_TRUE(standardOutput.empty());
}
