/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2026, Alfie Ardinata
 *
 * This file is part of the Windows whereis test suite.
 *
 * Exercise whereis implementation functions directly.
 *
 */

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#define WHEREIS_UNIT_TEST
#include "../whereis.cpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;
        const auto base = fs::temp_directory_path(error);
        if (error) { throw std::system_error { error, "cannot locate the temporary directory" }; }

        const auto prefix = L"whereis-unit-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-";

        for (unsigned int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = base / (prefix + std::to_wstring(attempt));
            error.clear();

            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }

            if (error && error != std::errc::file_exists) {
                throw std::system_error { error, "cannot create the temporary directory" };
            }
        }

        throw std::runtime_error { "could not allocate a unique temporary directory" };
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& path) {
    std::ofstream file { path, std::ios::binary };
    if (!file) { throw std::runtime_error { "cannot create test file: " + path.string() }; }
}

[[nodiscard]] std::optional<std::wstring> get_environment(std::wstring_view name) {
    const std::wstring key { name };
    const DWORD required = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (required == 0) { return std::nullopt; }

    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), required);
    if (written == 0 || written >= required) {
        throw std::system_error {
            static_cast<int>(GetLastError()), std::system_category(), "GetEnvironmentVariableW"
        };
    }

    value.resize(written);
    return value;
}

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::wstring name, std::optional<std::wstring> value)
        : name_ { std::move(name) }
        , previous_ { get_environment(name_) } {
        if (!SetEnvironmentVariableW(name_.c_str(), value ? value->c_str() : nullptr)) {
            throw std::system_error {
                static_cast<int>(GetLastError()), std::system_category(), "SetEnvironmentVariableW"
            };
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() {
        SetEnvironmentVariableW(name_.c_str(), previous_ ? previous_->c_str() : nullptr);
    }

private:
    std::wstring name_;
    std::optional<std::wstring> previous_;
};

struct FixedSearcher {
    std::vector<fs::path> matches;

    [[nodiscard]] std::vector<fs::path> find(const whereis::SearchRequest&) const { return matches; }
};

} // namespace

TEST(StringHelpers, TrimViewRemovesOuterWhitespace) {
    EXPECT_EQ(whereis::trim_view(L" \talpha beta\r\n"), L"alpha beta");
    EXPECT_TRUE(whereis::trim_view(L" \t\r\n").empty());
    EXPECT_EQ(whereis::trim_view(L"alpha beta"), L"alpha beta");
}

TEST(StringHelpers, UnquoteTrimsAndRemovesMatchingQuotes) {
    EXPECT_EQ(whereis::unquote(L"  \"C:\\Program Files\\Tool\"  "), L"C:\\Program Files\\Tool");
    EXPECT_EQ(whereis::unquote(L"  C:\\Tools\\bin  "), L"C:\\Tools\\bin");
    EXPECT_EQ(whereis::unquote(L"\"unmatched"), L"\"unmatched");
}

TEST(PathHelpers, NormalizedQueryUsesFilenameAndStripsOneCompressionSuffix) {
    EXPECT_EQ(whereis::normalized_query(L"C:\\Tools\\clang.exe.gz"), L"clang.exe");
    EXPECT_EQ(whereis::normalized_query(L"folder\\archive.TAR.GZ"), L"archive.TAR");
    EXPECT_EQ(whereis::normalized_query(L"python.exe"), L"python.exe");
    EXPECT_EQ(whereis::normalized_query(L"folder\\archive.tar.gz.xz"), L"archive.tar.gz");
}

TEST(ArgumentParser, ParsesCombinedFlags) {
    const std::array arguments { std::wstring_view { L"-abq" }, std::wstring_view { L"clang" } };
    const auto parsed = whereis::parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->all);
    EXPECT_TRUE(parsed->binaries);
    EXPECT_TRUE(parsed->quiet);
    EXPECT_FALSE(parsed->manuals);
    EXPECT_FALSE(parsed->sources);
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, EnablesAvailableCategoriesWhenNoneAreSelected) {
    const std::array arguments { std::wstring_view { L"clang" } };
    const auto parsed = whereis::parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->binaries);
    EXPECT_TRUE(parsed->manuals);
    EXPECT_TRUE(parsed->sources);
    EXPECT_TRUE(parsed->implicit_all_categories);
}

TEST(ArgumentParser, TreatsArgumentsAfterDoubleDashAsNames) {
    const std::array arguments {
        std::wstring_view { L"-b" }, std::wstring_view { L"--" }, std::wstring_view { L"-example" }
    };
    const auto parsed = whereis::parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->queries.size(), 1u);
    EXPECT_EQ(parsed->queries.front(), L"-example");
}

TEST(ArgumentParser, RejectsUnknownOptionsAndMissingDirectories) {
    const std::array unknown_arguments { std::wstring_view { L"--not-an-option" }, std::wstring_view { L"clang" } };
    const auto unknown = whereis::parse_arguments(std::span<const std::wstring_view> { unknown_arguments });

    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().find(L"Unknown option"), std::wstring::npos);

    const std::array directory_arguments { std::wstring_view { L"-B" } };
    const auto missing_directory
        = whereis::parse_arguments(std::span<const std::wstring_view> { directory_arguments });

    ASSERT_FALSE(missing_directory.has_value());
    EXPECT_NE(missing_directory.error().find(L"requires at least one directory"), std::wstring::npos);
}

TEST(ArgumentParser, SplitsAndDeduplicatesCustomDirectoryLists) {
    TemporaryDirectory temporary;
    const auto first = temporary.path() / L"bin";
    const auto second = temporary.path() / L"tools";
    fs::create_directories(first);
    fs::create_directories(second);

    std::wstring directories = first.native();
    directories.append(L"; ");
    directories.append(second.native());
    directories.push_back(L';');

    const std::array arguments {
        std::wstring_view { L"-B" }, std::wstring_view { directories }, std::wstring_view { L"-f" },
        std::wstring_view { L"clang" }
    };
    const auto parsed = whereis::parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->custom_binary_dirs);
    ASSERT_EQ(parsed->binary_dirs.size(), 2u);
    EXPECT_TRUE(whereis::same_path(parsed->binary_dirs[0], first));
    EXPECT_TRUE(whereis::same_path(parsed->binary_dirs[1], second));
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, RecognizesValidFlagClustersAndDirectorySwitches) {
    EXPECT_TRUE(whereis::is_flag_cluster(L"-ab"));
    EXPECT_TRUE(whereis::is_flag_cluster(L"-aB"));
    EXPECT_TRUE(whereis::is_flag_cluster(L"-abf"));
    EXPECT_FALSE(whereis::is_flag_cluster(L"-Ba"));
    EXPECT_FALSE(whereis::is_flag_cluster(L"--"));
    EXPECT_EQ(whereis::terminal_directory_switch(L"-aM"), L'M');
    EXPECT_EQ(whereis::terminal_directory_switch(L"-ab"), L'\0');
}

TEST(EnvironmentHelpers, ParsesAndDeduplicatesPathExtensions) {
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE;.CMD;.exe;BAT" } };

    const auto extensions = whereis::path_extensions();
    EXPECT_EQ(extensions, (std::vector<std::wstring> { L".EXE", L".CMD", L".BAT" }));
}

TEST(EnvironmentHelpers, UsesDefaultPathExtensionsWhenPathextIsUnset) {
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::nullopt };

    const auto extensions = whereis::path_extensions();
    ASSERT_EQ(extensions.size(), whereis::default_path_extensions.size());
    EXPECT_EQ(extensions.front(), L".COM");
    EXPECT_EQ(extensions[1], L".EXE");
}

TEST(DirectorySet, ComparesPathsWithoutCaseOrTrailingSeparator) {
    whereis::DirectorySet directories;
    directories.add(fs::path { L"C:\\Tools\\" });
    directories.add(fs::path { L"c:\\tools" });

    EXPECT_EQ(directories.values().size(), 1u);
}

TEST(OutputFormatting, QuotesPathsAndPreservesCategoryOrder) {
    whereis::SearchResult result;
    result.binaries.emplace_back(L"C:\\Program Files\\LLVM\\bin\\clang.exe");
    result.manuals.emplace_back(L"C:\\msys64\\usr\\share\\man\\man1\\clang.1");
    result.sources.emplace_back(L"D:\\src\\clang");

    EXPECT_EQ(whereis::make_output_line(L"clang", result, false),
        L"clang: \"C:\\Program Files\\LLVM\\bin\\clang.exe\" "
        L"C:\\msys64\\usr\\share\\man\\man1\\clang.1 D:\\src\\clang\n");

    EXPECT_EQ(whereis::make_output_line(L"clang", result, true),
        L"\"C:\\Program Files\\LLVM\\bin\\clang.exe\" "
        L"C:\\msys64\\usr\\share\\man\\man1\\clang.1 D:\\src\\clang\n");
}

TEST(OutputFormatting, PrintsAnEmptyResultLabelUnlessQuiet) {
    const whereis::SearchResult result;

    EXPECT_EQ(whereis::make_output_line(L"missing", result, false), L"missing:\n");
    EXPECT_TRUE(whereis::make_output_line(L"missing", result, true).empty());
}

TEST(SearchOrchestration, SetsMissingBitsForCategoriesWithoutMatches) {
    whereis::Options options;
    options.binaries = true;
    options.manuals = true;
    options.sources = true;
    options.all = true;
    options.no_recursive_sources = true;

    const FixedSearcher binaries { { fs::path { L"C:\\bin\\clang.exe" } } };
    const FixedSearcher manuals {};
    const FixedSearcher sources {};

    const auto result = whereis::search_one(L"clang", options, binaries, manuals, sources);

    ASSERT_EQ(result.binaries.size(), 1u);
    EXPECT_TRUE(result.manuals.empty());
    EXPECT_TRUE(result.sources.empty());
    EXPECT_EQ(result.missing, whereis::no_manual | whereis::no_source);
}

TEST(BinarySearcher, FindsAnExecutableInTheProvidedDirectory) {
    TemporaryDirectory temporary;
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE" } };
    const auto executable = temporary.path() / L"unit-tool.EXE";
    write_file(executable);

    const std::array directories { temporary.path() };
    const whereis::BinarySearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(whereis::same_path(matches.front(), executable));
}

TEST(ManualSearcher, FindsACompressedSectionedManualPage) {
    TemporaryDirectory temporary;
    const auto page_directory = temporary.path() / L"man1";
    fs::create_directories(page_directory);
    const auto page = page_directory / L"unit-tool.1.gz";
    write_file(page);

    const std::array directories { temporary.path() };
    const whereis::ManualSearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(whereis::same_path(matches.front(), page));
}

TEST(SourceSearcher, HandlesDirectRecursiveAndDisabledRecursiveLookup) {
    TemporaryDirectory temporary;
    const auto direct = temporary.path() / L"unit-direct";
    const auto recursive = temporary.path() / L"nested" / L"repository" / L"unit-recursive";
    fs::create_directories(direct);
    fs::create_directories(recursive);

    const std::array directories { temporary.path() };
    const whereis::SourceSearcher searcher { std::span<const fs::path> { directories } };

    const auto direct_matches = searcher.find({ L"unit-direct", false, false });
    ASSERT_EQ(direct_matches.size(), 1u);
    EXPECT_TRUE(whereis::same_path(direct_matches.front(), direct));

    const auto recursive_matches = searcher.find({ L"unit-recursive", false, false });
    ASSERT_EQ(recursive_matches.size(), 1u);
    EXPECT_TRUE(whereis::same_path(recursive_matches.front(), recursive));

    EXPECT_TRUE(searcher.find({ L"unit-recursive", false, true }).empty());
}
