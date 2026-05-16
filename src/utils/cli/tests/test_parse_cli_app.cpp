/**
 * @file test_parse_cli_app.cpp
 * @brief Unit tests for rpitx::cli::parseCliApp and assignFrequencyHz.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "captured_streams_mixin.h"
#include "cli_common.h"
#include "cli_parse_result.h"

namespace {
    using rpitx::cli::ParseResult;

    /**
     * @brief Drive parseCliApp with an inline argv list, owning the backing string storage internally.
     *
     * CLI11's parse() expects the canonical `char* argv[]` contract, so the underlying
     * std::string storage must outlive the parse call. Wrapping that lifetime here keeps
     * each test body a single call instead of three coupled lines (storage vector, char*
     * projection, static_cast<int> for argc).
     */
    ParseResult parseArgs(CLI::App& app, std::vector<std::string> argStorage) {
        std::vector<char*> argv;
        argv.reserve(argStorage.size());
        for (auto& arg: argStorage) {
            argv.push_back(arg.data());
        }
        return rpitx::cli::parseCliApp(app, static_cast<int>(argv.size()), argv.data());
    }
}  // namespace

class ParseCliAppTest : public CapturedStreamsMixin<::testing::Test> {};

/**
 * @brief Successful parse populates the registered option and stays silent on both streams.
 *
 * Feeds `--value 42` through CLI11 with a single int option bound, then asserts both the
 * return code and the bound value to catch a regression where parseCliApp's success
 * branch fails to actually invoke app.parse() - it is CLI11 (not the wrapper) that writes
 * into the bound variable, so a wrapper that returned Ok without calling parse would still
 * pass a return-code-only check. Both streams must stay empty on the success path; any
 * leak indicates the wrapper printed help or a diagnostic out-of-contract.
 */
TEST_F(ParseCliAppTest, SuccessPopulatesOptionAndProducesNoOutput) {
    CLI::App app{"test"};
    int boundValue{0};
    app.add_option("--value", boundValue, "");

    const ParseResult result{parseArgs(app, {"prog", "--value", "42"})};

    EXPECT_EQ(result, ParseResult::Ok);
    EXPECT_EQ(boundValue, 42);
    EXPECT_TRUE(capturedStderr().empty());
    EXPECT_TRUE(capturedStdout().empty());
}

/**
 * @brief An unknown flag routes the diagnostic and the help banner to stderr, leaving stdout untouched.
 *
 * Project contract for migrated binaries: parse errors emit `[ERROR] ...` followed by the
 * help banner on stderr, so callers that pipe stdout into another tool stay clean even on
 * failure. Asserting the `[ERROR]` prefix, the `Usage:` marker of CLI11's default help
 * formatter, and stdout emptiness pins down all three pieces of the contract - a
 * regression that mistakenly routed help to stdout, or dropped either half of the stderr
 * payload, would slip past a return-code-only check.
 */
TEST_F(ParseCliAppTest, UnknownFlagRoutesErrorAndHelpToStderr) {
    CLI::App app{"test"};

    const ParseResult result{parseArgs(app, {"prog", "--no-such-flag"})};

    EXPECT_EQ(result, ParseResult::Error);
    EXPECT_NE(capturedStderr().find("[ERROR]"), std::string::npos);
    EXPECT_NE(capturedStderr().find("Usage:"), std::string::npos);
    EXPECT_TRUE(capturedStdout().empty());
}

namespace {
    struct HelpFlagTestCase {
        std::string_view name;
        std::string_view flag;
    };

    /**
     * @brief Render a HelpFlagTestCase as its name plus the CLI flag literal for gtest listings.
     */
    void PrintTo(const HelpFlagTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {flag=\"" << testCase.flag << "\"}";
    }

    /**
     * @brief Both spellings of the help flag accepted by CLI11: GNU long form and POSIX short form.
     */
    std::vector<HelpFlagTestCase> makeHelpFlagTestCases() {
        return {
            HelpFlagTestCase{"LongForm", "--help"},
            HelpFlagTestCase{"ShortForm", "-h"},
        };
    }
}  // namespace

class ParseCliAppHelpFlagTest : public CapturedStreamsMixin<::testing::TestWithParam<HelpFlagTestCase>> {};

/**
 * @brief Either spelling of the help flag returns ParseResult::Help and routes the banner to stdout.
 *
 * The project contract distinguishes help (clean exit, banner on stdout for piping) from
 * errors (banner on stderr). Asserting that stderr stays empty and that stdout contains
 * the `Usage:` marker of CLI11's default help formatter pins down both halves of the
 * stream-routing contract; together with UnknownFlagRoutesErrorAndHelpToStderr the two
 * tests cover both CLI11 exception paths.
 */
TEST_P(ParseCliAppHelpFlagTest, RoutesBannerToStdoutAndReturnsHelp) {
    const auto& testCase{GetParam()};
    CLI::App app{"test"};

    const ParseResult result{parseArgs(app, {"prog", std::string{testCase.flag}})};

    EXPECT_EQ(result, ParseResult::Help);
    EXPECT_NE(capturedStdout().find("Usage:"), std::string::npos);
    EXPECT_TRUE(capturedStderr().empty());
}

INSTANTIATE_TEST_SUITE_P(SpellingMatrix, ParseCliAppHelpFlagTest, ::testing::ValuesIn(makeHelpFlagTestCases()),
                         [](const ::testing::TestParamInfo<HelpFlagTestCase>& info) {
                             return std::string{info.param.name};
                         });

class AssignFrequencyHzTest : public CapturedStreamsMixin<::testing::Test> {};

/**
 * @brief Successful conversion writes the parsed Hz to the out variable and stays silent on both streams.
 *
 * "100e6" is the canonical megahertz-range input that exercises the scientific-notation
 * branch of parseFrequencyHz; checking the exact 100_000_000 result distinguishes correct
 * integer-Hz resolution from any silent truncation. Asserting both streams stay empty
 * confirms the wrapper emits neither its diagnostic prefix nor any incidental stdout
 * chatter on the success path.
 */
TEST_F(AssignFrequencyHzTest, SuccessAssignsParsedHzAndStaysSilent) {
    std::uint64_t parsedHz{0};

    const ParseResult result{rpitx::cli::assignFrequencyHz("100e6", parsedHz)};

    EXPECT_EQ(result, ParseResult::Ok);
    EXPECT_EQ(parsedHz, 100'000'000ULL);
    EXPECT_TRUE(capturedStderr().empty());
    EXPECT_TRUE(capturedStdout().empty());
}

/**
 * @brief On rejection the stderr diagnostic contains both the fixed "[ERROR] Invalid --freq" prefix
 *        and the original input echoed in single quotes, while stdout stays clean.
 *
 * The diagnostic format is the user-visible contract migrated binaries rely on for their
 * error UX: splitting the check into two independent substring assertions catches a
 * regression in either half (e.g. the prefix typo'd, or the original input dropped from
 * the message). The Error-return and out-variable-preservation invariants are exercised
 * across the full input matrix by AssignFrequencyHzRejectionTest; the stdout-empty check
 * here guards against a regression where the failure path leaks any output to the
 * success-side stream.
 */
TEST_F(AssignFrequencyHzTest, FailureDiagnosticContainsErrorPrefixAndOriginalInput) {
    std::uint64_t parsedHz{0};

    const ParseResult result{rpitx::cli::assignFrequencyHz("nope", parsedHz)};

    EXPECT_EQ(result, ParseResult::Error);
    EXPECT_NE(capturedStderr().find("[ERROR] Invalid --freq"), std::string::npos);
    EXPECT_NE(capturedStderr().find("'nope'"), std::string::npos);
    EXPECT_TRUE(capturedStdout().empty());
}

namespace {
    struct AssignFrequencyHzRejectionTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render an AssignFrequencyHzRejectionTestCase as its name plus the rejected input
     *        for gtest listings and failure messages.
     */
    void PrintTo(const AssignFrequencyHzRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief Five representative rejection categories - empty, negative, zero, trailing garbage,
     *        non-numeric. Exhaustive parser-level coverage lives in test_parse_frequency.cpp;
     *        this sweep verifies the wrapper produces the same Error-return + preserved-out
     *        behaviour across each distinct rejection class.
     */
    std::vector<AssignFrequencyHzRejectionTestCase> makeAssignFrequencyHzRejectionTestCases() {
        return {
            AssignFrequencyHzRejectionTestCase{"Empty", ""},
            AssignFrequencyHzRejectionTestCase{"Negative", "-1"},
            AssignFrequencyHzRejectionTestCase{"Zero", "0"},
            AssignFrequencyHzRejectionTestCase{"TrailingGarbage", "100Hz"},
            AssignFrequencyHzRejectionTestCase{"NonNumeric", "abc"},
        };
    }
}  // namespace

class AssignFrequencyHzRejectionTest
    : public CapturedStreamsMixin<::testing::TestWithParam<AssignFrequencyHzRejectionTestCase>> {};

/**
 * @brief Across the rejection axis the wrapper returns Error and leaves the out variable untouched.
 *
 * parsedHz is preset to a recognizable 0xDEADBEEF sentinel and asserted to round-trip
 * unchanged, which catches a "zero out on failure" bug that a weaker "out != arbitrary
 * pre-test value" check would miss. The Error-return assertion is the second half of the
 * wrapper's failure-path invariant. Stream capture is handled by the fixture and not
 * asserted here - the diagnostic-format invariant lives in
 * FailureDiagnosticContainsErrorPrefixAndOriginalInput.
 */
TEST_P(AssignFrequencyHzRejectionTest, PreservesOutVariableAndReturnsError) {
    constexpr std::uint64_t kSentinelOutValue{0xDEADBEEFULL};
    const auto& testCase{GetParam()};
    std::uint64_t parsedHz{kSentinelOutValue};

    const ParseResult result{rpitx::cli::assignFrequencyHz(testCase.input, parsedHz)};

    EXPECT_EQ(result, ParseResult::Error);
    EXPECT_EQ(parsedHz, kSentinelOutValue);
}

INSTANTIATE_TEST_SUITE_P(MalformedInputMatrix, AssignFrequencyHzRejectionTest,
                         ::testing::ValuesIn(makeAssignFrequencyHzRejectionTestCases()),
                         [](const ::testing::TestParamInfo<AssignFrequencyHzRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });
