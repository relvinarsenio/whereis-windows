/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2002, Jörg Wunsch
 * Copyright © 2026, Alfie Ardinata
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <algorithm>
#include <array>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>
#include <winreg.h>

namespace fs = std::filesystem;

namespace {

constexpr int usage_error { 64 };
constexpr int data_error { 65 };
constexpr unsigned no_binary { 1u << 0 };
constexpr unsigned no_manual { 1u << 1 };
constexpr unsigned no_source { 1u << 2 };

constexpr std::array default_path_extensions { std::wstring_view { L".COM" }, std::wstring_view { L".EXE" },
    std::wstring_view { L".BAT" }, std::wstring_view { L".CMD" }, std::wstring_view { L".VBS" },
    std::wstring_view { L".VBE" }, std::wstring_view { L".JS" }, std::wstring_view { L".JSE" },
    std::wstring_view { L".WSF" }, std::wstring_view { L".WSH" }, std::wstring_view { L".MSC" } };

constexpr std::array manual_sections { std::wstring_view { L"1" }, std::wstring_view { L"2" },
    std::wstring_view { L"3" }, std::wstring_view { L"4" }, std::wstring_view { L"5" }, std::wstring_view { L"6" },
    std::wstring_view { L"7" }, std::wstring_view { L"8" }, std::wstring_view { L"9" }, std::wstring_view { L"1p" },
    std::wstring_view { L"3p" } };

constexpr std::array manual_compression_suffixes { std::wstring_view { L"" }, std::wstring_view { L".gz" },
    std::wstring_view { L".bz2" }, std::wstring_view { L".xz" }, std::wstring_view { L".zst" },
    std::wstring_view { L".html" }, std::wstring_view { L".htm" } };

constexpr std::array document_extensions { std::wstring_view { L".md" }, std::wstring_view { L".markdown" },
    std::wstring_view { L".html" }, std::wstring_view { L".htm" }, std::wstring_view { L".pdf" },
    std::wstring_view { L".txt" }, std::wstring_view { L".rst" }, std::wstring_view { L".adoc" } };

struct Options {
    bool all {};
    bool binaries {};
    bool manuals {};
    bool quiet {};
    bool sources {};
    bool unusual {};
    bool no_recursive_sources {};
    bool help {};
    bool implicit_all_categories {};

    bool custom_binary_dirs {};
    bool custom_manual_dirs {};
    bool custom_source_dirs {};

    std::vector<fs::path> binary_dirs;
    std::vector<fs::path> manual_dirs;
    std::vector<fs::path> source_dirs;
    std::vector<std::wstring> queries;
};

struct SearchRequest {
    std::wstring_view name;
    bool all {};
    bool skip_recursive {};
};

struct SearchResult {
    std::vector<fs::path> binaries;
    std::vector<fs::path> manuals;
    std::vector<fs::path> sources;
    unsigned missing {};
};

enum class HelpPosition {
    search,
    output
};

struct FlagSpec {
    wchar_t letter;
    bool Options::* member;
    HelpPosition position;
    std::wstring_view long_name;
    std::wstring_view help;
};

struct DirectorySpec {
    wchar_t letter;
    std::vector<fs::path> Options::* directories;
    bool Options::* custom;
    std::wstring_view help;
};

constexpr std::array flag_specs {
    FlagSpec { L'a', &Options::all, HelpPosition::search, {},
        L"Find all matches instead of the first match in each category" },
    FlagSpec { L'b', &Options::binaries, HelpPosition::search, {}, L"Search for executables" },
    FlagSpec { L'm', &Options::manuals, HelpPosition::search, {}, L"Search for manuals and documentation" },
    FlagSpec { L'q', &Options::quiet, HelpPosition::output, {}, L"Do not print the program name label" },
    FlagSpec { L's', &Options::sources, HelpPosition::search, {}, L"Search for source directories" },
    FlagSpec {
        L'u', &Options::unusual, HelpPosition::output, {}, L"Show only programs with at least one missing result" },
    FlagSpec { L'x', &Options::no_recursive_sources, HelpPosition::output, {}, L"Skip recursive source discovery" },
    FlagSpec { L'h', &Options::help, HelpPosition::output, L"--help", L"Show this help" },
};

constexpr std::array directory_specs {
    DirectorySpec {
        L'B', &Options::binary_dirs, &Options::custom_binary_dirs, L"Override executable search directories" },
    DirectorySpec { L'M', &Options::manual_dirs, &Options::custom_manual_dirs,
        L"Override manual/documentation search directories" },
    DirectorySpec { L'S', &Options::source_dirs, &Options::custom_source_dirs, L"Override source search directories" },
};

constexpr wchar_t list_end_letter { L'f' };
constexpr std::wstring_view list_end_help { L"Mark the end of directory lists" };
constexpr std::wstring_view options_end { L"--" };
constexpr std::wstring_view options_end_help { L"Treat all following arguments as program names" };

template <typename T>
concept SearchBackend = requires(const T& backend, const SearchRequest& request) {
    { backend.find(request) } -> std::same_as<std::vector<fs::path>>;
};

template <typename T>
concept TrimViewSource = std::is_lvalue_reference_v<T> || std::ranges::borrowed_range<std::remove_cvref_t<T>>;

class StringUtils final {
public:
    StringUtils() = delete;

    static constexpr std::string_view trim_sv(std::nullptr_t) = delete;

    template <typename T>
        requires std::constructible_from<std::string_view, T> && TrimViewSource<T>
    [[nodiscard]] static constexpr std::string_view trim_sv(T&& str) {
        return trim_impl(make_view<std::string_view>(std::forward<T>(str)));
    }

    template <typename T>
        requires std::constructible_from<std::wstring_view, T> && TrimViewSource<T>
    [[nodiscard]] static constexpr std::wstring_view trim_sv(T&& str) {
        return trim_impl(make_view<std::wstring_view>(std::forward<T>(str)));
    }

    static constexpr std::string trim(std::nullptr_t) = delete;

    template <typename T>
        requires std::constructible_from<std::string_view, T>
    [[nodiscard]] static constexpr std::string trim(T&& str) {
        return std::string { trim_impl(make_view<std::string_view>(std::forward<T>(str))) };
    }

    template <typename T>
        requires std::constructible_from<std::wstring_view, T>
    [[nodiscard]] static constexpr std::wstring trim(T&& str) {
        return std::wstring { trim_impl(make_view<std::wstring_view>(std::forward<T>(str))) };
    }

private:
    template <typename View, typename T>
        requires(!std::is_pointer_v<std::remove_cvref_t<T>>)
    [[nodiscard]] static constexpr View make_view(T&& source) {
        return View { std::forward<T>(source) };
    }

    template <typename View, typename T>
        requires std::is_pointer_v<std::remove_cvref_t<T>>
    [[nodiscard]] static constexpr View make_view(T&& source) {
        return source != nullptr ? View { source } : View {};
    }

    template <typename CharT> [[nodiscard]] static constexpr bool is_space(CharT ch) noexcept {
        return ch == CharT { ' ' } || ch == CharT { '\n' } || ch == CharT { '\r' } || ch == CharT { '\t' }
        || ch == CharT { '\v' } || ch == CharT { '\f' };
    }

    template <typename CharT>
    [[nodiscard]] static constexpr std::basic_string_view<CharT> trim_impl(std::basic_string_view<CharT> sv) noexcept {
        const auto first { std::ranges::find_if_not(sv, is_space<CharT>) };
        if (first == sv.end()) { return {}; }
        const auto last { std::ranges::find_if_not(sv | std::views::reverse, is_space<CharT>).base() };
        return { first, last };
    }
};

[[nodiscard]] std::wstring unquote(std::wstring value) {
    value = StringUtils::trim(value);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

[[nodiscard]] bool equal_ordinal(std::wstring_view left, std::wstring_view right, bool ignore_case = true) noexcept {
    if (left.size() > static_cast<std::size_t>(INT_MAX) || right.size() > static_cast<std::size_t>(INT_MAX)) {
        return false;
    }

    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
               static_cast<int>(right.size()), ignore_case ? TRUE : FALSE)
        == CSTR_EQUAL;
}

[[nodiscard]] std::optional<std::wstring> environment_value(std::wstring_view name) {
    const std::wstring key { name };
    const DWORD required { GetEnvironmentVariableW(key.c_str(), nullptr, 0) };
    if (required == 0) { return std::nullopt; }

    std::wstring value(required, L'\0');
    const DWORD written { GetEnvironmentVariableW(key.c_str(), value.data(), required) };
    if (written == 0 || written >= required) { return std::nullopt; }

    value.resize(written);
    return value;
}

[[nodiscard]] std::wstring expand_environment(std::wstring_view value) {
    const std::wstring source { value };
    const DWORD required { ExpandEnvironmentStringsW(source.c_str(), nullptr, 0) };
    if (required == 0) { return source; }

    std::wstring expanded(required, L'\0');
    const DWORD written { ExpandEnvironmentStringsW(source.c_str(), expanded.data(), required) };
    if (written == 0 || written > required) { return source; }

    expanded.resize(static_cast<std::size_t>(written) - 1);
    return expanded;
}

[[nodiscard]] fs::path normalize_path(std::wstring_view raw) {
    auto value { unquote(std::wstring { raw }) };
    value = expand_environment(value);

    if (value == L"~" || value.starts_with(L"~\\") || value.starts_with(L"~/")) {
        if (const auto profile { environment_value(L"USERPROFILE") }) { value.replace(0, 1, *profile); }
    }

    fs::path path { value };
    std::error_code error;
    if (path.is_relative()) {
        auto absolute { fs::absolute(path, error) };
        if (!error) { path = std::move(absolute); }
    }

    path.make_preferred();
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path()) { path = path.parent_path(); }
    return path;
}

[[nodiscard]] fs::path without_trailing_separator(fs::path path) {
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path()) { path = path.parent_path(); }
    return path;
}

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    const auto a { without_trailing_separator(left) };
    const auto b { without_trailing_separator(right) };
    return equal_ordinal(a.native(), b.native());
}

void append_unique_path(std::vector<fs::path>& paths, const fs::path& candidate) {
    const bool duplicate { std::ranges::any_of(
        paths, [&candidate](const fs::path& current) { return same_path(current, candidate); }) };
    if (!duplicate) { paths.push_back(candidate); }
}

[[nodiscard]] bool is_directory_path(const fs::path& path) noexcept {
    std::error_code error;
    return fs::is_directory(path, error);
}

[[nodiscard]] bool is_regular_file_path(const fs::path& path) noexcept {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

// App Execution Aliases are AppExecLink reparse points. The STL may report
// their file_type as unknown, while GetFileAttributesW still sees the entry.
[[nodiscard]] bool is_file_path(const fs::path& path) noexcept {
    const DWORD attributes { GetFileAttributesW(path.c_str()) };
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

class DirectorySet {
public:
    void add(std::wstring_view value) { add(normalize_path(value)); }

    void add(const fs::path& path) {
        if (!path.empty()) { append_unique_path(paths_, path); }
    }

    void add_list(std::wstring_view value) {
        for (const auto& segment : value | std::views::split(L';')) {
            const std::wstring part { segment.begin(), segment.end() };
            const auto trimmed { StringUtils::trim_sv(part) };
            if (!trimmed.empty()) { add(trimmed); }
        }
    }

    void add_existing(const fs::path& path) {
        if (is_directory_path(path)) { add(path); }
    }

    [[nodiscard]] bool empty() const noexcept { return paths_.empty(); }
    [[nodiscard]] std::span<const fs::path> values() const noexcept { return paths_; }

private:
    std::vector<fs::path> paths_;
};

[[nodiscard]] constexpr const FlagSpec* find_flag(wchar_t letter) noexcept {
    const auto found { std::ranges::find(flag_specs, letter, &FlagSpec::letter) };
    return found == flag_specs.end() ? nullptr : std::to_address(found);
}

[[nodiscard]] constexpr const FlagSpec* find_long_flag(std::wstring_view argument) noexcept {
    const auto found { std::ranges::find_if(flag_specs,
        [argument](const FlagSpec& spec) { return !spec.long_name.empty() && spec.long_name == argument; }) };
    return found == flag_specs.end() ? nullptr : std::to_address(found);
}

[[nodiscard]] constexpr const DirectorySpec* find_directory_spec(wchar_t letter) noexcept {
    const auto found { std::ranges::find(directory_specs, letter, &DirectorySpec::letter) };
    return found == directory_specs.end() ? nullptr : std::to_address(found);
}

[[nodiscard]] constexpr bool is_unique_letter(wchar_t letter) noexcept {
    const auto uses { std::ranges::count(flag_specs, letter, &FlagSpec::letter)
        + std::ranges::count(directory_specs, letter, &DirectorySpec::letter) + (letter == list_end_letter ? 1 : 0) };
    return uses == 1;
}

[[nodiscard]] constexpr bool all_letters_unique() noexcept {
    return std::ranges::all_of(flag_specs, [](const FlagSpec& spec) {
        return is_unique_letter(spec.letter);
    }) && std::ranges::all_of(directory_specs, [](const DirectorySpec& spec) { return is_unique_letter(spec.letter); });
}

static_assert(all_letters_unique(), "an option letter is used twice");

[[nodiscard]] bool is_simple_flag(wchar_t letter) noexcept {
    return find_flag(letter) != nullptr;
}

[[nodiscard]] constexpr bool ends_cluster(wchar_t letter) noexcept {
    return letter == list_end_letter || find_directory_spec(letter) != nullptr;
}

[[nodiscard]] bool is_flag_cluster(std::wstring_view argument) noexcept {
    if (argument.size() < 2 || argument.front() != L'-' || argument == options_end) { return false; }

    auto letters { argument.substr(1) };
    while (letters.size() > 1) {
        if (!is_simple_flag(letters.front())) { return false; }
        letters.remove_prefix(1);
    }

    return is_simple_flag(letters.front()) || ends_cluster(letters.front());
}

[[nodiscard]] bool ends_directory_list(std::wstring_view argument) noexcept {
    return argument == options_end || (is_flag_cluster(argument) && ends_cluster(argument.back()));
}

using ArgumentIterator = std::span<const std::wstring_view>::iterator;

void take_remaining_as_queries(Options& options, ArgumentIterator& argument, ArgumentIterator end) {
    for (; argument != end; ++argument) {
        options.queries.emplace_back(*argument);
    }
}

[[nodiscard]] std::expected<void, std::wstring> read_directory_list(
    const DirectorySpec& spec, Options& options, ArgumentIterator& argument, ArgumentIterator end) {
    DirectorySet parsed;

    for (; argument != end && !ends_directory_list(*argument); ++argument) {
        parsed.add_list(*argument);
    }

    if (parsed.empty()) {
        return std::unexpected(L"Option -" + std::wstring(1, spec.letter)
            + L" requires at least one directory before -f or another directory option.");
    }

    for (const auto& path : parsed.values()) {
        append_unique_path(options.*spec.directories, path);
    }

    options.*spec.custom = true;
    return {};
}

// The cluster is validated by is_flag_cluster(), so a terminating letter is always last.
[[nodiscard]] std::expected<void, std::wstring> parse_cluster(
    std::wstring_view cluster, Options& options, ArgumentIterator& argument, ArgumentIterator end) {
    for (const wchar_t letter : cluster) {
        if (const auto* flag { find_flag(letter) }) {
            options.*flag->member = true;
        } else if (letter == list_end_letter) {
            take_remaining_as_queries(options, argument, end);
        } else if (const auto* spec { find_directory_spec(letter) }) {
            return read_directory_list(*spec, options, argument, end);
        }
    }

    return {};
}

[[nodiscard]] std::expected<Options, std::wstring> parse_arguments(std::span<const std::wstring_view> arguments) {
    Options options;
    bool options_ended {};

    auto argument { arguments.begin() };
    const auto end { arguments.end() };

    while (argument != end) {
        const auto current { *argument++ };

        if (options_ended) {
            options.queries.emplace_back(current);
            continue;
        }

        if (current == options_end) {
            options_ended = true;
            continue;
        }

        if (const auto* flag { find_long_flag(current) }) {
            options.*flag->member = true;
            continue;
        }

        if (!current.starts_with(L'-') || current.size() == 1) {
            options.queries.emplace_back(current);
            take_remaining_as_queries(options, argument, end);
            continue;
        }

        if (!is_flag_cluster(current)) { return std::unexpected(L"Unknown option: " + std::wstring { current }); }

        if (auto parsed { parse_cluster(current.substr(1), options, argument, end) }; !parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
    }

    if (options.help) { return options; }
    if (options.queries.empty()) { return std::unexpected(L"No program names were specified."); }

    if (!options.binaries && !options.manuals && !options.sources) {
        options.binaries                = true;
        options.manuals                 = true;
        options.sources                 = true;
        options.implicit_all_categories = true;
    }

    return options;
}

void add_environment_directories(DirectorySet& target, std::wstring_view variable) {
    if (const auto value { environment_value(variable) }) { target.add_list(*value); }
}

using WindowsDirectoryGetter = UINT(WINAPI*)(LPWSTR, UINT);

void add_windows_directory(DirectorySet& target, WindowsDirectoryGetter getter) {
    std::wstring buffer(32768, L'\0');
    const UINT written { getter(buffer.data(), static_cast<UINT>(buffer.size())) };
    if (written == 0 || written >= static_cast<UINT>(buffer.size())) { return; }
    buffer.resize(written);
    target.add(std::wstring_view { buffer });
}

[[nodiscard]] fs::path program_files_path(std::wstring_view variable, std::wstring_view suffix) {
    const auto root { environment_value(variable) };
    if (!root) { return {}; }
    return normalize_path((fs::path { *root } / std::wstring { suffix }).native());
}

void configure_binary_directories(Options& options) {
    if (options.custom_binary_dirs) { return; }

    DirectorySet directories;
    add_environment_directories(directories, L"PATH");
    add_windows_directory(directories, GetSystemDirectoryW);
    add_windows_directory(directories, GetWindowsDirectoryW);

    options.binary_dirs.assign(directories.values().begin(), directories.values().end());
}

void configure_manual_directories(Options& options) {
    if (options.custom_manual_dirs) { return; }

    DirectorySet directories;
    add_environment_directories(directories, L"MANPATH");

    if (directories.empty()) {
        DirectorySet path_directories;
        add_environment_directories(path_directories, L"PATH");

        for (const auto& path : path_directories.values()) {
            if (!equal_ordinal(path.filename().native(), L"bin")) { continue; }
            directories.add_existing(path.parent_path() / L"share" / L"man");
            directories.add_existing(path.parent_path() / L"usr" / L"share" / L"man");
            directories.add_existing(path.parent_path().parent_path() / L"usr" / L"share" / L"man");
        }

        for (const auto variable : { std::wstring_view { L"MSYS2_ROOT" }, std::wstring_view { L"CYGWIN_ROOT" },
                 std::wstring_view { L"GIT_INSTALL_ROOT" } }) {
            if (const auto root { environment_value(variable) }) {
                directories.add_existing(fs::path { *root } / L"usr" / L"share" / L"man");
            }
        }

        directories.add_existing(program_files_path(L"ProgramFiles", L"Git\\usr\\share\\man"));
        directories.add_existing(program_files_path(L"ProgramFiles(x86)", L"Git\\usr\\share\\man"));
    }

    options.manual_dirs.assign(directories.values().begin(), directories.values().end());
}

void configure_source_directories(Options& options) {
    if (options.custom_source_dirs) { return; }

    DirectorySet directories;
    add_environment_directories(directories, L"SRCPATH");
    add_environment_directories(directories, L"PORTSDIR");

    if (const auto vcpkg { environment_value(L"VCPKG_ROOT") }) {
        directories.add_existing(fs::path { *vcpkg } / L"ports");
    }

    if (const auto profile { environment_value(L"USERPROFILE") }) {
        const fs::path home { *profile };
        directories.add_existing(home / L"source");
        directories.add_existing(home / L"source" / L"repos");
        directories.add_existing(home / L"src");
        directories.add_existing(home / L"Documents" / L"source");
        directories.add_existing(home / L"Documents" / L"GitHub");
        directories.add_existing(home / L"Documents" / L"Visual Studio 2022" / L"Projects");
        directories.add_existing(home / L"Documents" / L"Visual Studio 2026" / L"Projects");
    }

    if (const auto system_drive { environment_value(L"SystemDrive") }) {
        directories.add_existing(fs::path { *system_drive } / L"src");
        directories.add_existing(fs::path { *system_drive } / L"source");
    }

    options.source_dirs.assign(directories.values().begin(), directories.values().end());
}

void configure_directories(Options& options) {
    if (options.binaries) { configure_binary_directories(options); }
    if (options.manuals) { configure_manual_directories(options); }
    if (options.sources) { configure_source_directories(options); }

    if (options.implicit_all_categories) {
        if (options.binary_dirs.empty()) { options.binaries = false; }
        if (options.manual_dirs.empty()) { options.manuals = false; }
        if (options.source_dirs.empty()) { options.sources = false; }
    }
}

[[nodiscard]] std::wstring normalized_query(std::wstring_view argument) {
    fs::path input { argument };
    const fs::path filename { input.filename() };
    std::wstring name { filename.native() };

    constexpr std::array compression_suffixes { std::wstring_view { L".bz2" }, std::wstring_view { L".zst" },
        std::wstring_view { L".lzma" }, std::wstring_view { L".gz" }, std::wstring_view { L".xz" },
        std::wstring_view { L".Z" }, std::wstring_view { L".z" } };

    for (const auto suffix : compression_suffixes) {
        if (name.size() > suffix.size()
            && equal_ordinal(std::wstring_view { name }.substr(name.size() - suffix.size()), suffix)) {
            name.resize(name.size() - suffix.size());
            break;
        }
    }

    return name;
}

[[nodiscard]] std::vector<std::wstring> path_extensions() {
    std::vector<std::wstring> result;
    const auto value { environment_value(L"PATHEXT") };
    if (!value) {
        for (const auto extension : default_path_extensions) {
            result.emplace_back(extension);
        }
        return result;
    }

    for (const auto& segment : *value | std::views::split(L';')) {
        auto extension { unquote(std::wstring { segment.begin(), segment.end() }) };
        if (extension.empty()) { continue; }
        if (extension.front() != L'.') { extension.insert(extension.begin(), L'.'); }
        if (std::ranges::none_of(
                result, [&extension](const std::wstring& current) { return equal_ordinal(current, extension); })) {
            result.push_back(std::move(extension));
        }
    }

    return result;
}

[[nodiscard]] fs::path canonical_case(const fs::path& path) {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD written { GetLongPathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size())) };

    if (written == 0) { return path; }
    if (written >= buffer.size()) {
        buffer.resize(written);
        written = GetLongPathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    if (written == 0 || written >= buffer.size()) { return path; }
    buffer.resize(written);
    return fs::path { buffer };
}

class RegistryKey final {
public:
    explicit RegistryKey(HKEY key) noexcept
        : key_ { key } {}

    ~RegistryKey() noexcept {
        if (key_ != nullptr) { RegCloseKey(key_); }
    }

    RegistryKey(const RegistryKey&)            = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    [[nodiscard]] HKEY get() const noexcept { return key_; }

private:
    HKEY key_ {};
};

[[nodiscard]] std::optional<std::wstring> registry_app_path(std::wstring_view executable_name) {
    constexpr std::wstring_view key_prefix { L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" };
    const std::wstring key_name { std::wstring { key_prefix } + std::wstring { executable_name } };

    const std::array registry_roots { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    constexpr std::array<REGSAM, 2> registry_views { static_cast<REGSAM>(KEY_WOW64_64KEY),
        static_cast<REGSAM>(KEY_WOW64_32KEY) };
    constexpr DWORD wchar_size { static_cast<DWORD>(sizeof(wchar_t)) };

    for (const auto root : registry_roots) {
        for (const REGSAM view : registry_views) {
            HKEY raw_key {};
            if (RegOpenKeyExW(root, key_name.c_str(), 0, KEY_QUERY_VALUE | view, &raw_key) != ERROR_SUCCESS) {
                continue;
            }

            const RegistryKey key { raw_key };
            DWORD type {};
            DWORD bytes {};
            const LONG size_status { RegQueryValueExW(key.get(), nullptr, nullptr, &type, nullptr, &bytes) };

            if (size_status != ERROR_SUCCESS || bytes < wchar_size || bytes > 65536 || bytes % wchar_size != 0) {
                continue;
            }

            const DWORD buffer_size { bytes };
            std::vector<BYTE> storage(buffer_size);
            DWORD read_bytes { buffer_size };

            const LONG read_status { RegQueryValueExW(
                key.get(), nullptr, nullptr, &type, storage.data(), &read_bytes) };

            if (read_status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) { continue; }

            if (read_bytes < wchar_size || read_bytes > buffer_size || read_bytes % wchar_size != 0) { continue; }

            const auto character_count { static_cast<std::size_t>(read_bytes / wchar_size) };
            std::wstring value(character_count, L'\0');
            std::memcpy(value.data(), storage.data(), static_cast<std::size_t>(read_bytes));

            while (!value.empty() && value.back() == L'\0') {
                value.pop_back();
            }

            if (type == REG_EXPAND_SZ) { value = expand_environment(value); }
            value = unquote(std::move(value));
            if (!value.empty()) { return value; }
        }
    }

    return std::nullopt;
}

class BinarySearcher {
public:
    explicit BinarySearcher(std::span<const fs::path> directories)
        : directories_ { directories.begin(), directories.end() }
        , extensions_ { path_extensions() } {}

    [[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
        const auto file_names { build_file_names(request.name) };

        auto matches { std::vector<fs::path> {} };

        search_path_directories(file_names, request.all, matches);
        if (!request.all && !matches.empty()) { return matches; }

        search_registry_app_paths(file_names, request.all, matches);
        return matches;
    }

private:
    [[nodiscard]] std::vector<fs::path> build_file_names(std::wstring_view name) const {
        const fs::path requested { name };

        if (requested.has_extension() && is_executable_extension(requested.extension().native())) {
            return { requested };
        }

        auto file_names { std::vector<fs::path> {} };
        file_names.reserve(extensions_.size());
        for (const auto& extension : extensions_) {
            file_names.emplace_back(std::wstring { name } + extension);
        }
        return file_names;
    }

    // Resolve exactly like a PATH lookup: directory order first, then PATHEXT.
    void search_path_directories(
        const std::vector<fs::path>& file_names, bool all, std::vector<fs::path>& matches) const {
        for (const auto& directory : directories_) {
            for (const auto& file_name : file_names) {
                const auto candidate { directory / file_name };
                if (!is_executable_file(candidate)) { continue; }
                append_unique_path(matches, canonical_case(candidate));
                if (!all) { return; }
            }
        }
    }

    // App Paths is a ShellExecute/Run-style fallback, never ahead of PATH.
    void search_registry_app_paths(
        const std::vector<fs::path>& file_names, bool all, std::vector<fs::path>& matches) const {
        for (const auto& file_name : file_names) {
            const auto app_path { registry_app_path(file_name.filename().native()) };
            if (!app_path) { continue; }

            const auto candidate { normalize_path(*app_path) };
            if (!is_executable_file(candidate)) { continue; }
            append_unique_path(matches, canonical_case(candidate));
            if (!all) { return; }
        }
    }

    [[nodiscard]] bool is_executable_extension(std::wstring_view extension) const noexcept {
        constexpr std::array native_extensions { std::wstring_view { L".exe" }, std::wstring_view { L".com" },
            std::wstring_view { L".bat" }, std::wstring_view { L".cmd" } };
        if (std::ranges::any_of(native_extensions,
                [extension](std::wstring_view current) { return equal_ordinal(current, extension); })) {
            return true;
        }

        return std::ranges::any_of(
            extensions_, [extension](const std::wstring& current) { return equal_ordinal(current, extension); });
    }

    [[nodiscard]] bool is_executable_file(const fs::path& candidate) const noexcept {
        return is_file_path(candidate) && is_executable_extension(candidate.extension().native());
    }

    std::vector<fs::path> directories_;
    std::vector<std::wstring> extensions_;
};

class ManualSearcher {
public:
    explicit ManualSearcher(std::span<const fs::path> directories)
        : directories_ { directories.begin(), directories.end() } {}

    [[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
        return search_layouts(build_candidates(request.name), request.all);
    }

private:
    struct Candidate {
        fs::path path;
        bool last_in_group;
    };

    [[nodiscard]] static std::vector<Candidate> build_candidates(std::wstring_view name) {
        const std::wstring stem_base { name };
        auto candidates { std::vector<Candidate> {} };

        for (auto&& [section, suffix] : std::views::cartesian_product(manual_sections, manual_compression_suffixes)) {
            const auto stem { stem_base + L"." + std::wstring { section } };
            const auto section_name { std::format(L"man{}", section) };
            const auto page { stem + std::wstring { suffix } };

            candidates.push_back({ fs::path { section_name } / page, false });
            candidates.push_back({ fs::path { page }, true });
        }

        for (const auto extension : document_extensions) {
            candidates.push_back({ fs::path { stem_base + std::wstring { extension } }, true });
        }

        return candidates;
    }

    [[nodiscard]] std::vector<fs::path> search_layouts(const std::vector<Candidate>& candidates, bool all) const {
        auto matches { std::vector<fs::path> {} };
        auto layouts { directories_ | std::views::transform(manual_layouts) | std::views::join };

        // Return only after the last candidate in each pair has been checked.
        for (auto&& [layout, candidate] : std::views::cartesian_product(layouts, candidates)) {
            add_if_file(matches, layout / candidate.path);
            if (!all && candidate.last_in_group && !matches.empty()) { return matches; }
        }

        return matches;
    }

    [[nodiscard]] static std::vector<fs::path> manual_layouts(const fs::path& root) {
        return { root, root / L"man", root / L"doc", root / L"docs", root / L"share" / L"man" };
    }

    static void add_if_file(std::vector<fs::path>& matches, const fs::path& candidate) {
        if (!is_regular_file_path(candidate)) { return; }
        append_unique_path(matches, candidate);
    }

    std::vector<fs::path> directories_;
};

class SourceSearcher {
public:
    explicit SourceSearcher(std::span<const fs::path> directories)
        : directories_ { directories.begin(), directories.end() } {}

    [[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
        auto matches { std::vector<fs::path> {} };

        search_direct(request.name, request.all, matches);
        if (!request.all && !matches.empty()) { return matches; }

        if (!request.skip_recursive && !directories_.empty()) { search_recursive(request.name, request.all, matches); }

        return matches;
    }

private:
    void search_direct(std::wstring_view name, bool all, std::vector<fs::path>& matches) const {
        for (const auto& root : directories_) {
            const auto direct { root / name };
            if (!is_directory_path(direct)) { continue; }
            append_unique_path(matches, direct);
            if (!all) { return; }
        }
    }

    void search_recursive(std::wstring_view name, bool all, std::vector<fs::path>& matches) const {
        for (const auto& root : directories_) {
            search_tree(root, name, all, matches);
            if (!all && !matches.empty()) { return; }
        }
    }

    static void search_tree(const fs::path& root, std::wstring_view name, bool all, std::vector<fs::path>& matches) {
        std::error_code error;
        for (auto current { fs::recursive_directory_iterator {
                 root, fs::directory_options::skip_permission_denied, error } },
            end { fs::recursive_directory_iterator {} };
            !error && current != end; current.increment(error)) {
            const auto& entry_path { current->path() };
            std::error_code type_error;
            const bool directory { current->is_directory(type_error) };

            if (should_prune(entry_path.filename())) { current.disable_recursion_pending(); }

            if (!type_error && directory && equal_ordinal(entry_path.filename().native(), name)) {
                append_unique_path(matches, entry_path);
                if (!all) { return; }
            }
        }
    }

    [[nodiscard]] static bool should_prune(const fs::path& directory) noexcept {
        const auto& name { directory.native() };
        constexpr std::array ignored { std::wstring_view { L".git" }, std::wstring_view { L".hg" },
            std::wstring_view { L".svn" }, std::wstring_view { L".vs" }, std::wstring_view { L".cache" },
            std::wstring_view { L"node_modules" }, std::wstring_view { L"packages" }, std::wstring_view { L"build" },
            std::wstring_view { L"out" }, std::wstring_view { L"obj" } };
        return std::ranges::any_of(ignored, [&name](std::wstring_view item) { return equal_ordinal(name, item); });
    }

    std::vector<fs::path> directories_;
};

static_assert(SearchBackend<BinarySearcher>);
static_assert(SearchBackend<ManualSearcher>);
static_assert(SearchBackend<SourceSearcher>);

template <SearchBackend Binary, SearchBackend Manual, SearchBackend Source>
[[nodiscard]] SearchResult search_one(std::wstring_view name, const Options& options, const Binary& binaries,
    const Manual& manuals, const Source& sources) {
    const SearchRequest request { .name = name, .all = options.all, .skip_recursive = options.no_recursive_sources };
    SearchResult result;

    if (options.binaries) {
        result.binaries = binaries.find(request);
        if (result.binaries.empty()) { result.missing |= no_binary; }
    }

    if (options.manuals) {
        result.manuals = manuals.find(request);
        if (result.manuals.empty()) { result.missing |= no_manual; }
    }

    if (options.sources) {
        result.sources = sources.find(request);
        if (result.sources.empty()) { result.missing |= no_source; }
    }

    return result;
}

[[nodiscard]] std::wstring quote_path(const fs::path& path) {
    const auto& value { path.native() };

    if (std::ranges::find_first_of(value, std::wstring_view { L" \t" }) == value.end()) { return value; }

    constexpr std::wstring_view quote { L"\"" };
    const std::array parts { quote, std::wstring_view { value }, quote };
    const auto joined_view { parts | std::views::join };

    return std::wstring(joined_view.begin(), joined_view.end());
}

[[nodiscard]] std::wstring make_output_line(std::wstring_view name, const SearchResult& result, bool quiet) {
    std::wstring line;
    bool has_output {};

    if (!quiet) {
        line.append(name);
        line.push_back(L':');
        has_output = true;
    }

    const auto append_paths = [&line, &has_output](const std::vector<fs::path>& paths) {
        for (const auto& path : paths) {
            if (has_output) { line.push_back(L' '); }
            line.append(quote_path(path));
            has_output = true;
        }
    };

    append_paths(result.binaries);
    append_paths(result.manuals);
    append_paths(result.sources);

    if (has_output) { line.push_back(L'\n'); }
    return line;
}

class ConsoleModeRestorer final {
public:
    ConsoleModeRestorer(HANDLE handle, DWORD original) noexcept
        : handle_ { handle }
        , original_ { original } {}
    ~ConsoleModeRestorer() { SetConsoleMode(handle_, original_); }
    ConsoleModeRestorer(const ConsoleModeRestorer&)            = delete;
    ConsoleModeRestorer& operator=(const ConsoleModeRestorer&) = delete;

private:
    HANDLE handle_;
    DWORD original_;
};

class ConsoleOutput {
public:
    static void stdout_text(std::wstring_view text) { write(GetStdHandle(STD_OUTPUT_HANDLE), text); }
    static void stderr_text(std::wstring_view text) { write(GetStdHandle(STD_ERROR_HANDLE), text); }

private:
    struct ConsoleSink {};
    struct RedirectedSink {};

    static constexpr DWORD console_chunk { 32768 };

    static void write(HANDLE handle, std::wstring_view text) {
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty()) { return; }

        DWORD mode {};
        if (GetConsoleMode(handle, &mode)) {
            emit(ConsoleSink {}, handle, text, mode);
        } else {
            emit(RedirectedSink {}, handle, text);
        }
    }

    static void write_console(HANDLE handle, std::wstring_view text) {
        write_all<wchar_t>(text, console_chunk, [handle](const wchar_t* data, DWORD count, DWORD* written) {
            return WriteConsoleW(handle, data, count, written, nullptr);
        });
    }

    static void emit(ConsoleSink, HANDLE handle, std::wstring_view text, DWORD mode) {
        const DWORD forced { (mode | ENABLE_PROCESSED_OUTPUT) & ~DISABLE_NEWLINE_AUTO_RETURN };

        if (forced == mode) {
            write_console(handle, text);
            return;
        }
        if (!SetConsoleMode(handle, forced)) {
            write_console(handle, with_crlf(text));
            return;
        }
        const ConsoleModeRestorer restore { handle, mode };
        write_console(handle, text);
    }

    static void emit(RedirectedSink, HANDLE handle, std::wstring_view text) {
        const std::string bytes { to_utf8(text) };
        write_all<char>(bytes, MAXDWORD, [handle](const char* data, DWORD count, DWORD* written) {
            return WriteFile(handle, data, count, written, nullptr);
        });
    }

    template <typename Char, typename WriteSome>
        requires requires(WriteSome& fn, const Char* data, DWORD count, DWORD* written) {
            { fn(data, count, written) } -> std::convertible_to<bool>;
        }
    static void write_all(std::basic_string_view<Char> data, DWORD max_chunk, WriteSome write_some) {
        while (!data.empty()) {
            const auto chunk { static_cast<DWORD>(std::min<std::size_t>(data.size(), max_chunk)) };
            DWORD written {};
            if (!write_some(data.data(), chunk, &written) || written == 0) { return; }
            data.remove_prefix(written);
        }
    }

    // Fallback when SetConsoleMode fails: the console is assumed to be in a
    // non-default state where LF does not return to column zero, so prepend CR.
    static std::wstring with_crlf(std::wstring_view text) {
        std::wstring result;
        result.reserve(text.size() + 8);
        wchar_t previous {};
        for (const wchar_t character : text) {
            if (character == L'\n' && previous != L'\r') { result.push_back(L'\r'); }
            result.push_back(character);
            previous = character;
        }
        return result;
    }

    // Flags = 0 replaces invalid UTF-16 sequences with U+FFFD.
    // WideCharToMultiByte accepts an int source length.
    static std::string to_utf8(std::wstring_view text) {
        const int length { static_cast<int>(std::min<std::size_t>(text.size(), INT_MAX)) };
        const int required { WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr) };
        if (required <= 0) { return {}; }

        std::string utf8(static_cast<std::size_t>(required), '\0');
        const int converted { WideCharToMultiByte(
            CP_UTF8, 0, text.data(), length, utf8.data(), required, nullptr, nullptr) };
        return converted > 0 ? utf8 : std::string {};
    }
};

template <std::ranges::input_range Parts> [[nodiscard]] std::wstring join_text(Parts&& parts) {
    return std::forward<Parts>(parts) | std::views::join | std::ranges::to<std::wstring>();
}

[[nodiscard]] std::wstring flag_letters_text() {
    return flag_specs | std::views::transform(&FlagSpec::letter) | std::ranges::to<std::wstring>();
}

[[nodiscard]] std::wstring directory_usage_part(const DirectorySpec& spec) {
    const std::wstring terminator { &spec == &directory_specs.back() ? std::format(L" -{}", list_end_letter)
                                                                     : std::wstring {} };
    return std::format(L" [-{} dir ...{}]", spec.letter, terminator);
}

[[nodiscard]] std::wstring directory_usage_text() {
    return join_text(directory_specs | std::views::transform(directory_usage_part));
}

[[nodiscard]] std::wstring help_line(std::wstring_view label, std::wstring_view description, bool aligned = false) {
    return aligned ? std::format(L"  {:<12}{}\n", label, description) : std::format(L"  {}  {}\n", label, description);
}

[[nodiscard]] std::wstring flag_help_part(const FlagSpec& flag) {
    const auto label { flag.long_name.empty() ? std::format(L"-{}", flag.letter)
                                              : std::format(L"-{}, {}", flag.letter, flag.long_name) };
    return help_line(label, flag.help, !flag.long_name.empty());
}

[[nodiscard]] std::wstring directory_help_part(const DirectorySpec& spec) {
    return help_line(std::format(L"-{}", spec.letter), spec.help);
}

[[nodiscard]] constexpr bool has_long_name(const FlagSpec& flag) noexcept {
    return !flag.long_name.empty();
}

[[nodiscard]] std::wstring_view help_option_name() noexcept {
    const auto found { std::ranges::find_if(flag_specs, has_long_name) };
    return found == flag_specs.end() ? std::wstring_view {} : found->long_name;
}

[[nodiscard]] std::wstring flags_help(HelpPosition position) {
    const auto in_position { [position](const FlagSpec& flag) { return flag.position == position; } };
    return join_text(flag_specs | std::views::filter(in_position) | std::views::transform(flag_help_part));
}

[[nodiscard]] std::wstring directories_help() {
    return join_text(directory_specs | std::views::transform(directory_help_part));
}

[[nodiscard]] std::wstring help_text() {
    const std::array sections {
        flags_help(HelpPosition::search),
        directories_help(),
        help_line(std::format(L"-{}", list_end_letter), list_end_help),
        flags_help(HelpPosition::output),
        help_line(options_end, options_end_help),
    };
    return join_text(sections);
}

[[nodiscard]] std::wstring usage_text() {
    return std::format(L"whereis-windows - locate executables, documentation, and source directories\n"
                       L"Usage: whereis [-{}]{} name ...\n"
                       L"       whereis {}\n\n"
                       L"{}\n"
                       L"Directory lists use Windows' semicolon separator. Quote paths containing spaces.\n"
                       L"When no category is selected, -bms is assumed.\n",
        flag_letters_text(), directory_usage_text(), help_option_name(), help_text());
}

void print_usage(bool to_stderr = false) {
    const std::wstring text { usage_text() };

    if (to_stderr) {
        ConsoleOutput::stderr_text(text);
    } else {
        ConsoleOutput::stdout_text(text);
    }
}

[[nodiscard]] int run(const Options& options) {
    if (options.help) {
        print_usage();
        return 0;
    }

    if (!options.binaries && !options.manuals && !options.sources) {
        ConsoleOutput::stderr_text(L"whereis: no directories to search.\n");
        return data_error;
    }

    if (options.binaries && options.binary_dirs.empty()) {
        ConsoleOutput::stderr_text(L"whereis: no executable directories to search.\n");
        return data_error;
    }
    if (options.manuals && options.manual_dirs.empty()) {
        ConsoleOutput::stderr_text(L"whereis: no manual directories to search. Set MANPATH or use -M.\n");
        return data_error;
    }
    if (options.sources && options.source_dirs.empty()) {
        ConsoleOutput::stderr_text(L"whereis: no source directories to search. Set SRCPATH or use -S.\n");
        return data_error;
    }

    const BinarySearcher binaries { options.binary_dirs };
    const ManualSearcher manuals { options.manual_dirs };
    const SourceSearcher sources { options.source_dirs };

    for (const auto& requested : options.queries) {
        const auto name { normalized_query(requested) };
        if (name.empty()) { continue; }

        const auto result { search_one(name, options, binaries, manuals, sources) };
        if (options.unusual && result.missing == 0) { continue; }

        ConsoleOutput::stdout_text(make_output_line(name, result, options.quiet));
    }

    return 0;
}

} // namespace

#ifndef WHEREIS_UNIT_TEST
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::wstring_view> arguments;
    const auto argument_count { static_cast<std::size_t>(std::max(argc - 1, 0)) };
    const std::span argument_values { argv + 1, argument_count };
    arguments.reserve(argument_values.size());
    for (const auto* argument : argument_values) {
        arguments.emplace_back(argument);
    }

    auto parsed { parse_arguments(arguments) };
    if (!parsed) {
        ConsoleOutput::stderr_text(L"whereis: " + parsed.error() + L"\n\n");
        print_usage(true);
        return usage_error;
    }

    auto options { std::move(parsed.value()) };
    configure_directories(options);
    return run(options);
}
#endif
