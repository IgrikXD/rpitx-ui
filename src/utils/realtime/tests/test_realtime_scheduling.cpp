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
 * @brief Captures the diagnostics of each test and restores the scheduling of the test
 *        thread afterwards, so a real-time policy granted to one test does not leak into
 *        the next one.
 */
class EnableRealtimeSchedulingTest : public CapturedStreamsMixin<::testing::Test> {
protected:
    void SetUp() override {
        CapturedStreamsMixin::SetUp();
        savedPolicy_ = sched_getscheduler(0);
        ASSERT_NE(savedPolicy_, -1);
        ASSERT_EQ(sched_getparam(0, &savedParameters_), 0);
    }

    void TearDown() override {
        EXPECT_EQ(sched_setscheduler(0, savedPolicy_, &savedParameters_), 0);
        CapturedStreamsMixin::TearDown();
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
};

/**
 * @brief The thread becomes real-time at the lowest priority, or the refusal is reported and
 *        its scheduling stays as it was.
 *
 * Whether the kernel grants real-time scheduling depends on the privileges of the test
 * process (root or CAP_SYS_NICE), so both outcomes are accepted as long as they are complete
 * and consistent: a FIFO thread at the lowest real-time priority with an empty stderr, or an
 * untouched priority together with the [WARNING] diagnostic. The lowest priority already
 * runs ahead of every normally scheduled process and keeps the transmitter behind the
 * real-time threads of the system itself. An implementation that ignores the result of the
 * request fails the unprivileged run; one that warns unconditionally, or asks for a higher
 * priority, fails the privileged run.
 */
TEST_F(EnableRealtimeSchedulingTest, GrantsLowestRealtimePriorityOrWarns) {
    enableRealtimeScheduling();

    const bool isRealtime{sched_getscheduler(0) == SCHED_FIFO};
    sched_param currentParameters{};
    ASSERT_EQ(sched_getparam(0, &currentParameters), 0);
    const std::string& diagnostics{capturedStderr()};

    EXPECT_EQ(currentParameters.sched_priority, isRealtime ? sched_get_priority_min(SCHED_FIFO) : savedPriority());
    EXPECT_EQ(diagnostics.empty(), isRealtime);
    EXPECT_EQ(diagnostics.starts_with("[WARNING]"), isRealtime == false);
}
