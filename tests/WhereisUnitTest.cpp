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

#include <array>
#include <fstream>
#include <gtest/gtest.h>
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

    TemporaryDirectory(const TemporaryDirectory&)            = delete;
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
        throw std::system_error { static_cast<int>(GetLastError()), std::system_category(), "GetEnvironmentVariableW" };
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
            throw std::system_error { static_cast<int>(GetLastError()), std::system_category(),
                "SetEnvironmentVariableW" };
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&)            = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() { SetEnvironmentVariableW(name_.c_str(), previous_ ? previous_->c_str() : nullptr); }

private:
    std::wstring name_;
    std::optional<std::wstring> previous_;
};

struct FixedSearcher {
    std::vector<fs::path> matches;

    [[nodiscard]] std::vector<fs::path> find(const SearchRequest&) const { return matches; }
};

TEST(StringHelpers, TrimViewRemovesOuterWhitespace) {
    EXPECT_EQ(StringUtils::trim_sv(L" \talpha beta\r\n"), L"alpha beta");
    EXPECT_TRUE(StringUtils::trim_sv(L" \t\r\n").empty());
    EXPECT_EQ(StringUtils::trim_sv(L"alpha beta"), L"alpha beta");
    EXPECT_EQ(StringUtils::trim_sv(L"\v\fhello\v\f"), L"hello");
}

TEST(StringHelpers, TrimOwnedMatchesViewVariant) {
    const std::wstring source = L"  hello  ";
    EXPECT_EQ(StringUtils::trim(source), L"hello");
    EXPECT_EQ(StringUtils::trim_sv(source), L"hello");
    EXPECT_EQ(StringUtils::trim(std::wstring { L"  x  " }), L"x");
}

TEST(StringUtilsConcept, TrimViewSourceAcceptsLvaluesAndBorrowedRvalues) {
    static_assert(TrimViewSource<std::wstring&>);
    static_assert(TrimViewSource<const std::wstring&>);
    static_assert(TrimViewSource<std::wstring_view>);
    static_assert(TrimViewSource<const wchar_t(&)[8]>);
    static_assert(!TrimViewSource<std::wstring>);
    static_assert(!TrimViewSource<const wchar_t*>);
    static_assert(TrimViewSource<const wchar_t*&>); 
}

TEST(StringUtilsConcept, StringIsConstructibleToView) {
    static_assert(std::constructible_from<std::wstring_view, std::wstring&>);
    static_assert(std::constructible_from<std::wstring_view, const std::wstring&>);
}

TEST(StringHelpers, UnquoteTrimsAndRemovesMatchingQuotes) {
    EXPECT_EQ(unquote(L"  \"C:\\Program Files\\Tool\"  "), L"C:\\Program Files\\Tool");
    EXPECT_EQ(unquote(L"  C:\\Tools\\bin  "), L"C:\\Tools\\bin");
    EXPECT_EQ(unquote(L"\"unmatched"), L"\"unmatched");
}

TEST(PathHelpers, NormalizedQueryUsesFilenameAndStripsOneCompressionSuffix) {
    EXPECT_EQ(normalized_query(L"C:\\Tools\\clang.exe.gz"), L"clang.exe");
    EXPECT_EQ(normalized_query(L"folder\\archive.TAR.GZ"), L"archive.TAR");
    EXPECT_EQ(normalized_query(L"python.exe"), L"python.exe");
    EXPECT_EQ(normalized_query(L"folder\\archive.tar.gz.xz"), L"archive.tar.gz");
}

TEST(ArgumentParser, ParsesCombinedFlags) {
    const std::array arguments { std::wstring_view { L"-abq" }, std::wstring_view { L"clang" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

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
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->binaries);
    EXPECT_TRUE(parsed->manuals);
    EXPECT_TRUE(parsed->sources);
    EXPECT_TRUE(parsed->implicit_all_categories);
}

TEST(ArgumentParser, TreatsArgumentsAfterDoubleDashAsNames) {
    const std::array arguments { std::wstring_view { L"-b" }, std::wstring_view { L"--" },
        std::wstring_view { L"-example" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->queries.size(), 1u);
    EXPECT_EQ(parsed->queries.front(), L"-example");
}

TEST(ArgumentParser, RejectsUnknownOptionsAndMissingDirectories) {
    const std::array unknown_arguments { std::wstring_view { L"--not-an-option" }, std::wstring_view { L"clang" } };
    const auto unknown = parse_arguments(std::span<const std::wstring_view> { unknown_arguments });

    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().find(L"Unknown option"), std::wstring::npos);

    const std::array directory_arguments { std::wstring_view { L"-B" } };
    const auto missing_directory = parse_arguments(std::span<const std::wstring_view> { directory_arguments });

    ASSERT_FALSE(missing_directory.has_value());
    EXPECT_NE(missing_directory.error().find(L"requires at least one directory"), std::wstring::npos);
}

TEST(ArgumentParser, SplitsAndDeduplicatesCustomDirectoryLists) {
    TemporaryDirectory temporary;
    const auto first  = temporary.path() / L"bin";
    const auto second = temporary.path() / L"tools";
    fs::create_directories(first);
    fs::create_directories(second);

    std::wstring directories = first.native();
    directories.append(L"; ");
    directories.append(second.native());
    directories.push_back(L';');

    const std::array arguments { std::wstring_view { L"-B" }, std::wstring_view { directories },
        std::wstring_view { L"-f" }, std::wstring_view { L"clang" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->custom_binary_dirs);
    ASSERT_EQ(parsed->binary_dirs.size(), 2u);
    EXPECT_TRUE(same_path(parsed->binary_dirs[0], first));
    EXPECT_TRUE(same_path(parsed->binary_dirs[1], second));
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, RecognizesValidFlagClustersAndDirectoryListTerminators) {
    EXPECT_TRUE(is_flag_cluster(L"-ab"));
    EXPECT_TRUE(is_flag_cluster(L"-aB"));
    EXPECT_TRUE(is_flag_cluster(L"-abf"));
    EXPECT_FALSE(is_flag_cluster(L"-Ba"));
    EXPECT_FALSE(is_flag_cluster(L"--"));

    EXPECT_TRUE(ends_directory_list(L"--"));
    EXPECT_TRUE(ends_directory_list(L"-f"));
    EXPECT_TRUE(ends_directory_list(L"-M"));
    EXPECT_TRUE(ends_directory_list(L"-aB"));
    EXPECT_FALSE(ends_directory_list(L"-ab"));
    EXPECT_FALSE(ends_directory_list(L"foo"));
}

TEST(ArgumentParser, StopsOptionParsingAtFirstProgramName) {
    // BSD getopt: the first non-option argument ends option processing,
    // so later arguments (even flag-shaped ones) become program names.
    const std::array arguments { std::wstring_view { L"-b" }, std::wstring_view { L"foo" }, std::wstring_view { L"-a" },
        std::wstring_view { L"bar" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->binaries);
    EXPECT_FALSE(parsed->all);
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"foo", L"-a", L"bar" }));
}

TEST(ArgumentParser, DoubleDashBeforeNamesAllowsHyphenatedNames) {
    const std::array arguments { std::wstring_view { L"-b" }, std::wstring_view { L"--" },
        std::wstring_view { L"-example" }, std::wstring_view { L"-another" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"-example", L"-another" }));
}

TEST(ArgumentParser, AcceptsHelpWithoutProgramNames) {
    // -h and --help are the only options that may be used without names.
    const std::array short_arguments { std::wstring_view { L"-h" } };
    const auto short_parsed = parse_arguments(std::span<const std::wstring_view> { short_arguments });

    ASSERT_TRUE(short_parsed.has_value());
    EXPECT_TRUE(short_parsed->help);
    EXPECT_TRUE(short_parsed->queries.empty());

    const std::array long_arguments { std::wstring_view { L"--help" } };
    const auto long_parsed = parse_arguments(std::span<const std::wstring_view> { long_arguments });

    ASSERT_TRUE(long_parsed.has_value());
    EXPECT_TRUE(long_parsed->help);
    EXPECT_TRUE(long_parsed->queries.empty());
}

TEST(ArgumentParser, ClusterWithTerminalFTakesRemainingAsQueries) {
    // -f must be the last letter in a cluster; everything after it is a
    // program name, including strings that look like flags.
    // The explicit -b prevents implicit-all from masking whether the -m
    // after the name is processed. If it were, manuals would be true.
    const std::array arguments { std::wstring_view { L"-b" }, std::wstring_view { L"-af" },
        std::wstring_view { L"foo" }, std::wstring_view { L"-m" }, std::wstring_view { L"bar" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->all);
    EXPECT_TRUE(parsed->binaries);
    EXPECT_FALSE(parsed->manuals);
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"foo", L"-m", L"bar" }));
}

TEST(ArgumentParser, RejectsTerminalLetterBeforeOtherFlags) {
    const std::array arguments { std::wstring_view { L"-fa" }, std::wstring_view { L"foo" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_FALSE(parsed.has_value());
    EXPECT_NE(parsed.error().find(L"Unknown option"), std::wstring::npos);
}

TEST(ArgumentParser, TreatsSingleDashAsProgramName) {
    // A lone "-" is a program name, following Unix convention.
    const std::array arguments { std::wstring_view { L"-" }, std::wstring_view { L"foo" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"-", L"foo" }));
}

TEST(ArgumentParser, HandlesMultipleDirectorySwitches) {
    TemporaryDirectory temporary;
    const auto bin_dir = temporary.path() / L"bin";
    const auto man_dir = temporary.path() / L"man";
    fs::create_directories(bin_dir);
    fs::create_directories(man_dir);

    const std::array arguments { std::wstring_view { L"-B" }, std::wstring_view { bin_dir.native() },
        std::wstring_view { L"-M" }, std::wstring_view { man_dir.native() }, std::wstring_view { L"-f" },
        std::wstring_view { L"clang" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->binary_dirs.size(), 1u);
    EXPECT_TRUE(same_path(parsed->binary_dirs[0], bin_dir));
    ASSERT_EQ(parsed->manual_dirs.size(), 1u);
    EXPECT_TRUE(same_path(parsed->manual_dirs[0], man_dir));
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, CombinesFlagAndDirectorySwitchInOneCluster) {
    // -aB processes 'a' first, then 'B' consumes the following directories.
    TemporaryDirectory temporary;
    const auto bin_dir = temporary.path() / L"bin";
    fs::create_directories(bin_dir);

    const std::array arguments { std::wstring_view { L"-aB" }, std::wstring_view { bin_dir.native() },
        std::wstring_view { L"-f" }, std::wstring_view { L"clang" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->all);
    EXPECT_TRUE(parsed->custom_binary_dirs);
    ASSERT_EQ(parsed->binary_dirs.size(), 1u);
    EXPECT_TRUE(same_path(parsed->binary_dirs[0], bin_dir));
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, AccumulatesMultipleBinaryDirectorySwitches) {
    TemporaryDirectory temporary;
    const auto first  = temporary.path() / L"first";
    const auto second = temporary.path() / L"second";
    fs::create_directories(first);
    fs::create_directories(second);

    const std::array arguments { std::wstring_view { L"-B" }, std::wstring_view { first.native() },
        std::wstring_view { L"-B" }, std::wstring_view { second.native() }, std::wstring_view { L"-f" },
        std::wstring_view { L"clang" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->binary_dirs.size(), 2u);
    EXPECT_TRUE(same_path(parsed->binary_dirs[0], first));
    EXPECT_TRUE(same_path(parsed->binary_dirs[1], second));
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"clang" }));
}

TEST(ArgumentParser, RejectsTripleDash) {
    const std::array arguments { std::wstring_view { L"---" }, std::wstring_view { L"foo" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_FALSE(parsed.has_value());
    EXPECT_NE(parsed.error().find(L"Unknown option"), std::wstring::npos);
}

TEST(ArgumentParser, RejectsAnEmptyDirectoryList) {
    const std::array arguments { std::wstring_view { L"-B" }, std::wstring_view { L"-f" },
        std::wstring_view { L"foo" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_FALSE(parsed.has_value());
    EXPECT_NE(parsed.error().find(L"requires at least one directory"), std::wstring::npos);
}

TEST(ArgumentParser, ClusterEndingWithFClosesTheDirectoryList) {
    const std::array arguments { std::wstring_view { L"-B" }, std::wstring_view { L"C:\\Tools" },
        std::wstring_view { L"-af" }, std::wstring_view { L"foo" } };
    const auto parsed = parse_arguments(std::span<const std::wstring_view> { arguments });

    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->all);
    ASSERT_EQ(parsed->binary_dirs.size(), 1u);
    EXPECT_EQ(parsed->queries, (std::vector<std::wstring> { L"foo" }));
}

TEST(EnvironmentHelpers, ParsesAndDeduplicatesPathExtensions) {
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE;.CMD;.exe;BAT" } };

    const auto extensions = path_extensions();
    EXPECT_EQ(extensions, (std::vector<std::wstring> { L".EXE", L".CMD", L".BAT" }));
}

TEST(EnvironmentHelpers, UsesDefaultPathExtensionsWhenPathextIsUnset) {
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::nullopt };

    const auto extensions = path_extensions();
    ASSERT_EQ(extensions.size(), default_path_extensions.size());
    EXPECT_EQ(extensions.front(), L".COM");
    EXPECT_EQ(extensions[1], L".EXE");
}

TEST(DirectorySet, ComparesPathsWithoutCaseOrTrailingSeparator) {
    DirectorySet directories;
    directories.add(fs::path { L"C:\\Tools\\" });
    directories.add(fs::path { L"c:\\tools" });

    EXPECT_EQ(directories.values().size(), 1u);
}

TEST(OutputFormatting, QuotesPathsAndPreservesCategoryOrder) {
    SearchResult result;
    result.binaries.emplace_back(L"C:\\Program Files\\LLVM\\bin\\clang.exe");
    result.manuals.emplace_back(L"C:\\msys64\\usr\\share\\man\\man1\\clang.1");
    result.sources.emplace_back(L"D:\\src\\clang");

    EXPECT_EQ(make_output_line(L"clang", result, false),
        L"clang: \"C:\\Program Files\\LLVM\\bin\\clang.exe\" "
        L"C:\\msys64\\usr\\share\\man\\man1\\clang.1 D:\\src\\clang\n");

    EXPECT_EQ(make_output_line(L"clang", result, true),
        L"\"C:\\Program Files\\LLVM\\bin\\clang.exe\" "
        L"C:\\msys64\\usr\\share\\man\\man1\\clang.1 D:\\src\\clang\n");
}

TEST(OutputFormatting, PrintsAnEmptyResultLabelUnlessQuiet) {
    const SearchResult result;

    EXPECT_EQ(make_output_line(L"missing", result, false), L"missing:\n");
    EXPECT_TRUE(make_output_line(L"missing", result, true).empty());
}

TEST(SearchOrchestration, SetsMissingBitsForCategoriesWithoutMatches) {
    Options options;
    options.binaries             = true;
    options.manuals              = true;
    options.sources              = true;
    options.all                  = true;
    options.no_recursive_sources = true;

    const FixedSearcher binaries { { fs::path { L"C:\\bin\\clang.exe" } } };
    const FixedSearcher manuals {};
    const FixedSearcher sources {};

    const auto result = search_one(L"clang", options, binaries, manuals, sources);

    ASSERT_EQ(result.binaries.size(), 1u);
    EXPECT_TRUE(result.manuals.empty());
    EXPECT_TRUE(result.sources.empty());
    EXPECT_EQ(result.missing, no_manual | no_source);
}

TEST(BinarySearcher, FindsAnExecutableInTheProvidedDirectory) {
    TemporaryDirectory temporary;
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE" } };
    const auto executable = temporary.path() / L"unit-tool.EXE";
    write_file(executable);

    const std::array directories { temporary.path() };
    const BinarySearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), canonical_case(executable)));
}

TEST(BinarySearcher, FindsDottedNameWithAppendedPathext) {
    // A dot in the name (python3.12, tool.v2) does not mean an executable
    // extension is present, so PATHEXT is appended. Matches cmd.exe.
    TemporaryDirectory temporary;
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE" } };
    const auto executable = temporary.path() / L"python3.12.EXE";
    write_file(executable);

    const std::array directories { temporary.path() };
    const BinarySearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"python3.12", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), canonical_case(executable)));
}

TEST(BinarySearcher, UsesLiteralWhenExtensionIsInPathext) {
    // An explicit executable extension (tool.exe) is looked up literally,
    // never as tool.exe.EXE.
    TemporaryDirectory temporary;
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".EXE" } };
    const auto executable = temporary.path() / L"tool.EXE";
    write_file(executable);

    const std::array directories { temporary.path() };
    const BinarySearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"tool.exe", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), canonical_case(executable)));
}

TEST(BinarySearcher, RespectsPathextOrderWhenMultipleExtensionsMatch) {
    // With PATHEXT=.COM;.EXE and both files present, .COM wins.
    TemporaryDirectory temporary;
    ScopedEnvironmentVariable pathext { L"PATHEXT", std::wstring { L".COM;.EXE" } };
    const auto com_file = temporary.path() / L"unit-tool.COM";
    const auto exe_file = temporary.path() / L"unit-tool.EXE";
    write_file(com_file);
    write_file(exe_file);

    const std::array directories { temporary.path() };
    const BinarySearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), canonical_case(com_file)));
}

TEST(ManualSearcher, FindsACompressedSectionedManualPage) {
    TemporaryDirectory temporary;
    const auto page_directory = temporary.path() / L"man1";
    fs::create_directories(page_directory);
    const auto page = page_directory / L"unit-tool.1.gz";
    write_file(page);

    const std::array directories { temporary.path() };
    const ManualSearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), page));
}

TEST(ManualSearcher, FindsStandaloneDocument) {
    // Covers the second phase of ManualSearcher::find: loose documents are
    // checked after every section and suffix misses.
    TemporaryDirectory temporary;
    const auto document = temporary.path() / L"unit-tool.md";
    write_file(document);

    const std::array directories { temporary.path() };
    const ManualSearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(same_path(matches.front(), document));
}

TEST(SourceSearcher, HandlesDirectRecursiveAndDisabledRecursiveLookup) {
    TemporaryDirectory temporary;
    const auto direct  = temporary.path() / L"unit-direct";
    fs::path recursive = temporary.path();
    for (unsigned int level = 0; level < 10; ++level) {
        recursive /= L"nested";
    }
    recursive /= L"unit-recursive";

    fs::create_directories(direct);
    fs::create_directories(recursive);

    const std::array directories { temporary.path() };
    const SourceSearcher searcher { std::span<const fs::path> { directories } };

    const auto direct_matches = searcher.find({ L"unit-direct", false, false });
    ASSERT_EQ(direct_matches.size(), 1u);
    EXPECT_TRUE(same_path(direct_matches.front(), direct));

    const auto recursive_matches = searcher.find({ L"unit-recursive", false, false });
    ASSERT_EQ(recursive_matches.size(), 1u);
    EXPECT_TRUE(same_path(recursive_matches.front(), recursive));

    EXPECT_TRUE(searcher.find({ L"unit-recursive", false, true }).empty());
}

TEST(SourceSearcher, PrunesIgnoredDirectories) {
    // .git and other ignored directories are pruned, so matches inside
    // them are not reported.
    TemporaryDirectory temporary;
    const auto hidden = temporary.path() / L".git" / L"unit-tool";
    fs::create_directories(hidden);

    const std::array directories { temporary.path() };
    const SourceSearcher searcher { std::span<const fs::path> { directories } };
    const auto matches = searcher.find({ L"unit-tool", false, false });

    EXPECT_TRUE(matches.empty());
}

} // namespace
