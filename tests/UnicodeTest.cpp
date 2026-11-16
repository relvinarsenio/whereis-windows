
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2026, Alfie Ardinata
 *
 * This file is part of the Windows whereis test suite.
 *
 * Verify that filenames containing unpaired UTF-16 surrogates are
 * preserved in redirected output using the UTF-8 replacement character.
 *
 */

#include "TestSupport.hpp"

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <system_error>
#include <windows.h>

namespace {

constexpr std::wstring_view invalid_filename_prefix = L"whereis-invalid-";

constexpr std::wstring_view invalid_filename_suffix = L".exe";

constexpr std::string_view expected_name_utf8 = "whereis-invalid-\xEF\xBF\xBD.exe";

TEST(UnicodeOutput, ReplacesUnpairedUtf16SurrogateWithReplacementCharacter) {
    TestSupport::TemporaryDirectory directory { L"whereis-utf16-" };

    std::wstring query { invalid_filename_prefix };
    query.push_back(static_cast<wchar_t>(0xD800));
    query.append(invalid_filename_suffix);

    const auto fixture_path = directory.path() / query;

    try {
        [[maybe_unused]] auto fixture = TestSupport::open_file(fixture_path, GENERIC_WRITE, 0, CREATE_NEW);
    } catch (const std::system_error& error) {
        if (error.code().category() == std::system_category() && error.code().value() == ERROR_INVALID_NAME) {
            GTEST_SKIP() << "The filesystem rejects filenames containing unpaired UTF-16 surrogates.";
        }

        throw;
    }

    TestSupport::ProcessOptions options;
    options.timeout_ms = 15'000;

    const auto result = TestSupport::run_whereis({ L"-b", L"-B", directory.path().native(), L"-f", query }, options);

    ASSERT_EQ(result.exit_code, static_cast<DWORD>(0)) << "stdout:\n"
                                                       << result.stdout_output << "\nstderr:\n"
                                                       << result.stderr_output;

    const auto& contents = result.stdout_output;

    ASSERT_FALSE(contents.empty());
    ASSERT_EQ(contents.back(), '\n');
    ASSERT_EQ(contents.find('\n'), contents.size() - 1);
    ASSERT_EQ(contents.find('\r'), std::string::npos);

    const auto line = std::string_view { contents }.substr(0, contents.size() - 1);

    const auto separator = line.find(": ");
    ASSERT_NE(separator, std::string_view::npos);

    const auto label       = line.substr(0, separator);
    const auto result_path = line.substr(separator + 2);

    EXPECT_EQ(label, expected_name_utf8);
    EXPECT_NE(result_path.find(expected_name_utf8), std::string_view::npos);
}

} // namespace