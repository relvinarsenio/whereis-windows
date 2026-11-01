
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

#include <array>
#include <cstddef>
#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <windows.h>

namespace {

static_assert(sizeof(wchar_t) == sizeof(char16_t));

constexpr DWORD process_timeout_ms = 15'000;

constexpr std::wstring_view invalid_filename_prefix = L"whereis-invalid-";

constexpr std::wstring_view invalid_filename_suffix = L".exe";

constexpr std::string_view expected_name_utf8 = "whereis-invalid-\xEF\xBF\xBD.exe";

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : handle_ { handle } {}

    UniqueHandle(const UniqueHandle&)            = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_ { std::exchange(other.handle_, INVALID_HANDLE_VALUE) } {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) { reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE)); }

        return *this;
    }

    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    void reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
        if (*this) { CloseHandle(handle_); }

        handle_ = replacement;
    }

private:
    HANDLE handle_;
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;

        const auto base = std::filesystem::temp_directory_path(error);

        if (error) { throw std::system_error { error, "cannot locate the temporary directory" }; }

        std::wstring prefix { L"whereis-utf16-" };
        prefix += std::to_wstring(GetCurrentProcessId());
        prefix += L"-";
        prefix += std::to_wstring(GetTickCount64());
        prefix += L"-";

        for (unsigned int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = base / (prefix + std::to_wstring(attempt));

            error.clear();

            if (std::filesystem::create_directory(candidate, error)) {
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

    TemporaryDirectory(TemporaryDirectory&&)            = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[noreturn]] void throw_last_error(std::string_view operation) {
    const auto error = GetLastError();

    throw std::system_error { static_cast<int>(error), std::system_category(), std::string { operation } };
}

[[nodiscard]] UniqueHandle open_file(
    const std::filesystem::path& path, DWORD access, DWORD share_mode, DWORD creation_disposition, bool inheritable) {
    SECURITY_ATTRIBUTES security_attributes { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr,
        inheritable ? TRUE : FALSE };

    UniqueHandle file { CreateFileW(
        path.c_str(), access, share_mode, &security_attributes, creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr) };

    if (!file) { throw_last_error("CreateFileW"); }

    return file;
}

/*
 * Follow the Windows command-line quoting rules, including backslashes
 * immediately preceding quotation marks and the closing quotation mark.
 */
void append_quoted_argument(std::wstring& command_line, std::wstring_view argument) {
    if (!command_line.empty()) { command_line.push_back(L' '); }

    command_line.push_back(L'"');

    std::size_t backslash_count = 0;

    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslash_count;
            continue;
        }

        if (character == L'"') {
            command_line.append(backslash_count * 2 + 1, L'\\');

            command_line.push_back(L'"');
        } else {
            command_line.append(backslash_count, L'\\');

            command_line.push_back(character);
        }

        backslash_count = 0;
    }

    command_line.append(backslash_count * 2, L'\\');

    command_line.push_back(L'"');
}

[[nodiscard]] std::string read_all(HANDLE file) {
    LARGE_INTEGER beginning {};

    if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) { throw_last_error("SetFilePointerEx"); }

    std::string contents;
    std::array<char, 4096> buffer {};

    for (;;) {
        DWORD bytes_read = 0;

        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
            throw_last_error("ReadFile");
        }

        if (bytes_read == 0) { break; }

        contents.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }

    return contents;
}

[[nodiscard]] std::string run_whereis(
    const std::filesystem::path& executable, const std::filesystem::path& search_directory, std::wstring_view query) {
    const auto output_path = search_directory / L"stdout.bin";

    auto output
        = open_file(output_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, true);

    auto input = open_file(
        std::filesystem::path { L"NUL" }, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, true);

    STARTUPINFOW startup {};
    startup.cb         = static_cast<DWORD>(sizeof(STARTUPINFOW));
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdInput  = input.get();
    startup.hStdOutput = output.get();
    startup.hStdError  = output.get();

    std::wstring command_line;

    append_quoted_argument(command_line, executable.native());

    append_quoted_argument(command_line, L"-b");
    append_quoted_argument(command_line, L"-B");
    append_quoted_argument(command_line, search_directory.native());
    append_quoted_argument(command_line, L"-f");
    append_quoted_argument(command_line, query);

    PROCESS_INFORMATION process_info {};

    if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
            nullptr, &startup, &process_info)) {
        throw_last_error("CreateProcessW");
    }

    UniqueHandle process { process_info.hProcess };
    UniqueHandle thread { process_info.hThread };

    const auto wait_result = WaitForSingleObject(process.get(), process_timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        if (!TerminateProcess(process.get(), ERROR_TIMEOUT)) { throw_last_error("TerminateProcess"); }

        WaitForSingleObject(process.get(), INFINITE);

        throw std::runtime_error { "whereis did not exit before the test timeout" };
    }

    if (wait_result != WAIT_OBJECT_0) {
        const auto wait_error = GetLastError();

        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.get(), INFINITE);

        throw std::system_error { static_cast<int>(wait_error), std::system_category(), "WaitForSingleObject" };
    }

    DWORD exit_code = 0;

    if (!GetExitCodeProcess(process.get(), &exit_code)) { throw_last_error("GetExitCodeProcess"); }

    if (!FlushFileBuffers(output.get())) { throw_last_error("FlushFileBuffers"); }

    const auto contents = read_all(output.get());

    if (exit_code != 0) {
        throw std::runtime_error { "whereis returned exit code " + std::to_string(exit_code) + "\n" + contents };
    }

    return contents;
}

[[nodiscard]] std::filesystem::path whereis_executable() {
    constexpr wchar_t variable[] = L"WHEREIS_EXE";

    const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
    if (required == 0) {
        throw std::runtime_error { "WHEREIS_EXE is not set." };
    }

    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(variable, value.data(), required);
    if (written == 0 || written >= required) { throw_last_error("GetEnvironmentVariableW"); }

    value.resize(written);
    return std::filesystem::path { std::move(value) };
}

} // namespace

TEST(UnicodeOutput, ReplacesUnpairedUtf16SurrogateWithReplacementCharacter) {
    TemporaryDirectory directory;

    std::wstring query { invalid_filename_prefix };
    query.push_back(static_cast<wchar_t>(0xD800));
    query.append(invalid_filename_suffix);

    const auto fixture_path = directory.path() / query;

    try {
        [[maybe_unused]] auto fixture = open_file(fixture_path, GENERIC_WRITE, 0, CREATE_NEW, false);
    } catch (const std::system_error& error) {
        if (error.code().category() == std::system_category() && error.code().value() == ERROR_INVALID_NAME) {
            GTEST_SKIP() << "The filesystem rejects filenames containing unpaired UTF-16 surrogates.";
        }

        throw;
    }

    const auto contents = run_whereis(whereis_executable(), directory.path(), query);

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
