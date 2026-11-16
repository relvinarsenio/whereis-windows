/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2026, Alfie Ardinata
 *
 * This file is part of the Windows whereis test suite.
 *
 * Verify command-line behavior and end-to-end functionality of whereis.
 *
 */

#include "TestSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <gtest/gtest.h>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

using ProcessResult = TestSupport::ProcessResult;

[[nodiscard]] std::string lowercase_ascii(std::string_view value) {
    std::string result { value };

    std::ranges::transform(
        result, result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    return result;
}

[[nodiscard]] bool contains_case_insensitive(std::string_view output, std::string_view expected) {
    return lowercase_ascii(output).find(lowercase_ascii(expected)) != std::string::npos;
}

[[nodiscard]] std::size_t count_occurrences_case_insensitive(std::string_view output, std::string_view expected) {
    const auto lowercase_output   = lowercase_ascii(output);
    const auto lowercase_expected = lowercase_ascii(expected);

    std::size_t count    = 0;
    std::size_t position = 0;

    while ((position = lowercase_output.find(lowercase_expected, position)) != std::string::npos) {
        ++count;
        position += lowercase_expected.size();
    }

    return count;
}

[[nodiscard]] std::string_view first_line(std::string_view output) noexcept {
    const auto end = output.find_first_of("\r\n");

    return output.substr(0, end);
}

[[nodiscard]] std::optional<fs::path> find_system_where_executable() {
    std::array<wchar_t, MAX_PATH> system_directory {};

    const UINT length = GetSystemDirectoryW(system_directory.data(), static_cast<UINT>(system_directory.size()));

    if (length == 0 || length >= static_cast<UINT>(system_directory.size())) { return std::nullopt; }

    fs::path executable { std::wstring { system_directory.data(), static_cast<std::size_t>(length) } };

    executable /= L"where.exe";

    std::error_code error;

    if (!fs::is_regular_file(executable, error) || error) { return std::nullopt; }

    return executable;
}

class WhereisAcceptanceTest : public ::testing::Test {
protected:
    [[nodiscard]] const fs::path& root() const noexcept { return temporary_directory_.path(); }

    [[nodiscard]] fs::path create_directory(std::wstring_view relative_path) const {
        const fs::path directory = root() / fs::path { relative_path };

        fs::create_directories(directory);

        return directory;
    }

    void create_file(const fs::path& path) const {
        fs::create_directories(path.parent_path());

        TestSupport::write_file(path, "test");
    }

    [[nodiscard]] static ProcessResult run(std::initializer_list<std::wstring_view> arguments) {
        return TestSupport::run_whereis(arguments);
    }

private:
    TestSupport::TemporaryDirectory temporary_directory_ { L"whereis-acceptance-" };
};

/*
 * Help and command-line errors.
 */

TEST_F(WhereisAcceptanceTest, LongHelpOptionPrintsUsageAndDocumentsOptions) {
    const auto result = run({ L"--help" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "Usage: whereis [-abmqsuxh]"));

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis --help"));

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "-h, --help"));

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "Treat all following arguments as program names"));
}

TEST_F(WhereisAcceptanceTest, ShortHelpOptionPrintsUsage) {
    const auto result = run({ L"-h" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "Usage: whereis"));
}

TEST_F(WhereisAcceptanceTest, MissingQueryReturnsUsageError) {
    const auto result = run({});

    EXPECT_EQ(result.exit_code, static_cast<DWORD>(64));

    EXPECT_TRUE(contains_case_insensitive(result.stderr_output, "No program names")) << "stderr:\n"
                                                                                     << result.stderr_output;
}

TEST_F(WhereisAcceptanceTest, UnknownOptionReturnsUsageError) {
    const auto result = run({ L"--not-a-real-option", L"sample" });

    EXPECT_EQ(result.exit_code, static_cast<DWORD>(64));

    EXPECT_TRUE(contains_case_insensitive(result.stderr_output, "Unknown option")) << "stderr:\n"
                                                                                   << result.stderr_output;
}

/*
 * Binary lookup.
 */

TEST_F(WhereisAcceptanceTest, FindsDottedExecutableNameInDirectoryWithSpaces) {
    const auto binary_directory = create_directory(L"binary one");

    create_file(binary_directory / L"whereis-cmake-foo-1.2.exe");

    const auto result = run({ L"-B", binary_directory.native(), L"-f", L"whereis-cmake-foo-1.2" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-foo-1.2.exe"));

    EXPECT_NE(result.stdout_output.find('"'), std::string::npos) << "Paths containing spaces should be quoted.\n"
                                                                 << result.stdout_output;
}

TEST_F(WhereisAcceptanceTest, AllOptionReturnsEveryBinaryMatch) {
    const auto first_directory  = create_directory(L"binary one");
    const auto second_directory = create_directory(L"binary-two");

    create_file(first_directory / L"whereis-cmake-foo-1.2.exe");
    create_file(second_directory / L"whereis-cmake-foo-1.2.exe");

    const auto result
        = run({ L"-abB", first_directory.native(), second_directory.native(), L"-f", L"whereis-cmake-foo-1.2" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_EQ(count_occurrences_case_insensitive(result.stdout_output, "whereis-cmake-foo-1.2.exe"), 2u)
        << "-a should return both test executables.\n"
        << result.stdout_output;
}

TEST_F(WhereisAcceptanceTest, PreservesUnicodeQueriesAndPathsInRedirectedOutput) {
    constexpr std::wstring_view unicode_directory_name = L"unicode \u8DEF\u5F84";
    constexpr std::wstring_view unicode_filename       = L"\u5DE5\u5177.exe";
    constexpr std::wstring_view unicode_query          = L"\u5DE5\u5177";

    constexpr std::string_view expected_filename_utf8 = "\xE5\xB7\xA5\xE5\x85\xB7.exe";

    constexpr std::string_view expected_directory_utf8 = "unicode \xE8\xB7\xAF\xE5\xBE\x84";

    constexpr std::string_view replacement_character_utf8 = "\xEF\xBF\xBD";

    const auto unicode_directory = root() / fs::path { unicode_directory_name };

    fs::create_directories(unicode_directory);

    create_file(unicode_directory / fs::path { unicode_filename });

    const auto result = run({ L"-b", L"-B", unicode_directory.native(), L"-f", unicode_query });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_NE(result.stdout_output.find(expected_filename_utf8), std::string::npos)
        << "Unicode filename was not preserved.\n"
        << result.stdout_output;

    const auto redirected_result = run({ L"-q", L"-b", L"-B", unicode_directory.native(), L"-f", unicode_query });

    ASSERT_EQ(redirected_result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                                  << redirected_result.stdout_output << "\nstderr:\n"
                                                                  << redirected_result.stderr_output;

    EXPECT_NE(redirected_result.stdout_output.find(expected_filename_utf8), std::string::npos)
        << "Redirected output does not contain the filename as UTF-8.";

    EXPECT_NE(redirected_result.stdout_output.find(expected_directory_utf8), std::string::npos)
        << "Redirected output does not contain the directory as UTF-8.";

    EXPECT_TRUE(redirected_result.stdout_output.ends_with('\n')) << "Redirected output must end with LF.";

    EXPECT_EQ(redirected_result.stdout_output.find('\r'), std::string::npos)
        << "Redirected output must not contain CR.";

    EXPECT_EQ(redirected_result.stdout_output.find(replacement_character_utf8), std::string::npos)
        << "Valid Unicode output must not contain U+FFFD.";
}

TEST_F(WhereisAcceptanceTest, QuietOptionSuppressesNameLabel) {
    const auto binary_directory = create_directory(L"quiet-binary");

    create_file(binary_directory / L"whereis-cmake-quiet.exe");

    const auto result = run({ L"-q", L"-b", L"-B", binary_directory.native(), L"-f", L"whereis-cmake-quiet" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_FALSE(contains_case_insensitive(result.stdout_output, "whereis-cmake-quiet:"));

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-quiet.exe"));
}

TEST_F(WhereisAcceptanceTest, PathAndPathextControlExecutableLookup) {
    const auto search_directory = create_directory(L"pathext");

    create_file(search_directory / L"whereis-cmake-pathext.cmd");

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable path_variable { L"PATH",
        std::optional<std::wstring> { search_directory.native() } };

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable pathext_variable { L"PATHEXT",
        std::optional<std::wstring> { L".CMD" } };

    const auto result = run({ L"-b", L"whereis-cmake-pathext" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-pathext.cmd"));
}

/*
 * BSD command-line option semantics.
 */

TEST_F(WhereisAcceptanceTest, DoubleDashAllowsNamesBeginningWithHyphens) {
    const auto binary_directory = create_directory(L"leading-dash");

    create_file(binary_directory / L"-whereis-cmake-leading.exe");

    const auto result = run({ L"-B", binary_directory.native(), L"--", L"-whereis-cmake-leading" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "-whereis-cmake-leading.exe"));
}

TEST_F(WhereisAcceptanceTest, UnusualOptionIncludesNamesMissingRequestedCategories) {
    const auto binary_directory = create_directory(L"unusual");

    create_file(binary_directory / L"cmd.exe");

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable path_variable { L"PATH",
        std::optional<std::wstring> { binary_directory.native() } };

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable pathext_variable { L"PATHEXT",
        std::optional<std::wstring> { L".EXE" } };

    const auto result = run({ L"-u", L"-b", L"cmd", L"whereis-cmake-absent" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-absent:"));

    EXPECT_FALSE(contains_case_insensitive(result.stdout_output, "cmd:"));
}

TEST_F(WhereisAcceptanceTest, UnusualOptionSuppressesNamesFoundInEveryRequestedCategory) {
    const auto binary_directory = create_directory(L"unusual-found");

    create_file(binary_directory / L"cmd.exe");

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable path_variable { L"PATH",
        std::optional<std::wstring> { binary_directory.native() } };

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable pathext_variable { L"PATHEXT",
        std::optional<std::wstring> { L".EXE" } };

    const auto result = run({ L"-u", L"-b", L"cmd" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(result.stdout_output.empty()) << "-u should suppress a name found in every requested category.\n"
                                              << result.stdout_output;
}

TEST_F(WhereisAcceptanceTest, AcceptsClusteredBinaryAndAllOptions) {
    const auto binary_directory = create_directory(L"option-clusters");

    create_file(binary_directory / L"cmd.exe");

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable path_variable { L"PATH",
        std::optional<std::wstring> { binary_directory.native() } };

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable pathext_variable { L"PATHEXT",
        std::optional<std::wstring> { L".EXE" } };

    const std::array arguments { std::initializer_list<std::wstring_view> { L"-ab", L"cmd" },
        std::initializer_list<std::wstring_view> { L"-abf", L"cmd" },
        std::initializer_list<std::wstring_view> { L"-baf", L"cmd" } };

    for (const auto& test_arguments : arguments) {
        const auto result = run(test_arguments);

        ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                           << result.stdout_output << "\nstderr:\n"
                                                           << result.stderr_output;

        EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "cmd:"));
    }
}

TEST_F(WhereisAcceptanceTest, AcceptsAllOptionWithBinaryDirectorySwitchAndQueryTerminator) {
    const auto binary_directory = create_directory(L"all-option-directory");

    create_file(binary_directory / L"whereis-cmake-foo-1.2.exe");

    const auto result = run({ L"-aB", binary_directory.native(), L"-f", L"whereis-cmake-foo-1.2" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-foo-1.2.exe"));
}

/*
 * Manual page and standalone documentation lookup.
 */

TEST_F(WhereisAcceptanceTest, FindsSectionedManualPage) {
    const auto manual_directory = create_directory(L"manual");

    create_file(manual_directory / L"man1" / L"whereis-cmake-manual.1");

    const auto result = run({ L"-m", L"-M", manual_directory.native(), L"-f", L"whereis-cmake-manual" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-manual.1"));
}

TEST_F(WhereisAcceptanceTest, FindsStandaloneDocumentation) {
    const auto manual_directory = create_directory(L"documentation");

    create_file(manual_directory / L"docs" / L"whereis-cmake-doc.md");

    const auto result = run({ L"-m", L"-M", manual_directory.native(), L"-f", L"whereis-cmake-doc" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-doc.md"));
}

/*
 * Source directory lookup.
 */

TEST_F(WhereisAcceptanceTest, FindsDirectSourceDirectory) {
    const auto source_directory = create_directory(L"source");

    fs::create_directories(source_directory / L"whereis-cmake-direct");

    const auto result = run({ L"-s", L"-S", source_directory.native(), L"-f", L"whereis-cmake-direct" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-direct"));
}

TEST_F(WhereisAcceptanceTest, FindsRecursiveSourceDirectory) {
    const auto source_directory = create_directory(L"source");

    fs::create_directories(source_directory / L"nested" / L"repository" / L"whereis-cmake-recursive");

    const auto result = run({ L"-s", L"-S", source_directory.native(), L"-f", L"whereis-cmake-recursive" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_TRUE(contains_case_insensitive(result.stdout_output, "whereis-cmake-recursive"));
}

TEST_F(WhereisAcceptanceTest, NoRecursiveOptionSkipsNestedSourceDirectories) {
    const auto source_directory = create_directory(L"source");

    fs::create_directories(source_directory / L"nested" / L"repository" / L"whereis-cmake-recursive");

    const auto result = run({ L"-s", L"-x", L"-S", source_directory.native(), L"-f", L"whereis-cmake-recursive" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    EXPECT_EQ(result.stdout_output.find("whereis-cmake-recursive: "), std::string::npos)
        << "-x unexpectedly returned a recursively discovered source path.\n"
        << result.stdout_output;
}

/*
 * Redirected output formatting.
 */

TEST_F(WhereisAcceptanceTest, RedirectedOutputEndsWithLfInsteadOfCrlf) {
    const auto binary_directory = create_directory(L"redirected-output");

    create_file(binary_directory / L"cmd.exe");

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable path_variable { L"PATH",
        std::optional<std::wstring> { binary_directory.native() } };

    [[maybe_unused]] TestSupport::ScopedEnvironmentVariable pathext_variable { L"PATHEXT",
        std::optional<std::wstring> { L".EXE" } };

    const auto result = run({ L"-q", L"-b", L"cmd" });

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    ASSERT_FALSE(result.stdout_output.empty());

    EXPECT_TRUE(result.stdout_output.ends_with('\n')) << "Redirected output must end with LF.";

    EXPECT_EQ(result.stdout_output.find("\r\n"), std::string::npos) << "Redirected output must not contain CRLF.";
}

/*
 * Compatibility with Microsoft's where.exe.
 */

TEST_F(WhereisAcceptanceTest, FirstResultsMatchWhereExeForCommonSystemBinaries) {
    const auto where_executable = find_system_where_executable();

    if (!where_executable) { GTEST_SKIP() << "where.exe was not found in the Windows system directory."; }

    struct ProgramName {
        std::wstring_view wide;
        std::string_view utf8;
    };

    constexpr std::array programs { ProgramName { L"pwsh", "pwsh" }, ProgramName { L"python", "python" },
        ProgramName { L"winget", "winget" }, ProgramName { L"wt", "wt" }, ProgramName { L"find", "find" },
        ProgramName { L"sort", "sort" } };

    std::size_t compared_programs = 0;

    for (const auto& program : programs) {
        const auto where_result = TestSupport::run_process(*where_executable, { program.wide });

        if (where_result.exit_code != 0) { continue; }

        const auto whereis_result = run({ L"-b", program.wide });

        ASSERT_EQ(whereis_result.exit_code, static_cast<DWORD>(0))
            << "whereis lookup failed for " << program.utf8 << "\nstdout:\n"
            << whereis_result.stdout_output << "\nstderr:\n"
            << whereis_result.stderr_output;

        const auto where_first = first_line(where_result.stdout_output);
        auto whereis_first     = first_line(whereis_result.stdout_output);

        const std::string prefix = std::string { program.utf8 } + ": ";

        ASSERT_TRUE(whereis_first.starts_with(prefix)) << "Unexpected whereis output for " << program.utf8 << ":\n"
                                                       << whereis_result.stdout_output;

        whereis_first.remove_prefix(prefix.size());

        if (whereis_first.size() >= 2 && whereis_first.front() == '"' && whereis_first.back() == '"') {
            whereis_first.remove_prefix(1);
            whereis_first.remove_suffix(1);
        }

        EXPECT_EQ(lowercase_ascii(where_first), lowercase_ascii(whereis_first))
            << "First result differs from where.exe for " << program.utf8 << "\nwhere.exe: " << where_first
            << "\nwhereis: " << whereis_first;

        ++compared_programs;
    }

    if (compared_programs == 0) { GTEST_SKIP() << "where.exe could not resolve any of the comparison programs."; }
}

} // namespace