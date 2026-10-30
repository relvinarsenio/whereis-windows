/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2026, Alfie Ardinata
 *
 * This file is part of the Windows whereis test suite.
 */

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <windows.h>

namespace {

using namespace std::string_view_literals;

constexpr DWORD process_timeout_ms                    = 15'000;
constexpr std::wstring_view invalid_filename_prefix   = L"whereis-invalid-"sv;
constexpr std::wstring_view invalid_filename_suffix   = L".exe"sv;
constexpr std::string_view utf8_replacement_character = "\xEF\xBF\xBD"sv;

class unique_handle {
public:
    explicit unique_handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : handle_ { handle } {}

    unique_handle(const unique_handle&)            = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept
        : handle_ { std::exchange(other.handle_, INVALID_HANDLE_VALUE) } {}

    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) { reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE)); }
        return *this;
    }

    ~unique_handle() { reset(); }

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

class temporary_directory {
public:
    temporary_directory() {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) { throw std::system_error { error, "cannot locate the temporary directory" }; }

        const auto prefix = L"whereis-utf16-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-";

        for (unsigned int attempt = 0; attempt != 32; ++attempt) {
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

    temporary_directory(const temporary_directory&)            = delete;
    temporary_directory& operator=(const temporary_directory&) = delete;
    temporary_directory(temporary_directory&&)                 = delete;
    temporary_directory& operator=(temporary_directory&&)      = delete;

    ~temporary_directory() {
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

[[nodiscard]] unique_handle create_inheritable_file(
    const std::filesystem::path& path, DWORD access, DWORD share_mode, DWORD creation_disposition) {
    SECURITY_ATTRIBUTES security_attributes { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE };

    unique_handle file { CreateFileW(
        path.c_str(), access, share_mode, &security_attributes, creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr) };

    if (!file) { throw_last_error("CreateFileW"); }

    return file;
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

        if (bytes_read == 0) { return contents; }

        contents.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }
}

[[nodiscard]] std::size_t count_occurrences(std::string_view contents, std::string_view needle) noexcept {
    std::size_t count  = 0;
    std::size_t offset = 0;

    while ((offset = contents.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }

    return count;
}

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error { std::string { message } }; }
}

[[nodiscard]] std::string run_test(const std::filesystem::path& whereis_executable) {
    temporary_directory directory;

    std::wstring query { invalid_filename_prefix };
    query.push_back(static_cast<wchar_t>(0xD800));
    query.append(invalid_filename_suffix);

    const auto fixture_path = directory.path() / query;
    const auto output_path  = directory.path() / L"stdout.bin";

    {
        unique_handle fixture { CreateFileW(
            fixture_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) };

        if (!fixture) { throw_last_error("CreateFileW could not create the lone-surrogate filename"); }
    }

    auto output = create_inheritable_file(
        output_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS);
    auto input = create_inheritable_file(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);

    STARTUPINFOW startup {};
    startup.cb         = static_cast<DWORD>(sizeof(STARTUPINFOW));
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdInput  = input.get();
    startup.hStdOutput = output.get();
    startup.hStdError  = output.get();

    std::wstring command_line;
    append_quoted_argument(command_line, whereis_executable.native());
    append_quoted_argument(command_line, L"-b");
    append_quoted_argument(command_line, L"-B");
    append_quoted_argument(command_line, directory.path().native());
    append_quoted_argument(command_line, L"-f");
    append_quoted_argument(command_line, query);

    PROCESS_INFORMATION process_info {};
    if (!CreateProcessW(whereis_executable.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process_info)) {
        throw_last_error("CreateProcessW");
    }

    unique_handle process { process_info.hProcess };
    unique_handle thread { process_info.hThread };

    const auto wait_result = WaitForSingleObject(process.get(), process_timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), ERROR_TIMEOUT);
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
    require(exit_code == 0, "whereis returned a non-zero exit status");

    if (!FlushFileBuffers(output.get())) { throw_last_error("FlushFileBuffers"); }

    const auto contents = read_all(output.get());

    // The replacement must appear in both the printed query label and result path.
    require(count_occurrences(contents, utf8_replacement_character) == 2,
        "expected U+FFFD in both the query label and result path");
    require(
        count_occurrences(contents, "whereis-invalid-") == 2, "the query label and result path were not both printed");
    require(count_occurrences(contents, ".exe") == 2, "the query label and result path were incomplete");
    require(!contents.empty() && contents.back() == '\n', "redirected output does not end with LF");
    require(contents.find('\r') == std::string::npos, "redirected output unexpectedly contains a carriage return");
    require(contents.find('\n') == contents.size() - 1, "expected exactly one output line");

    return "[PASS] malformed UTF-16 is replaced with UTF-8 U+FFFD in the query label and result path";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::fputs("[FAIL] expected the path to whereis.exe\n", stderr);
        return 1;
    }

    try {
        const auto result = run_test(std::filesystem::path { argv[1] });
        std::puts(result.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[FAIL] %s\n", error.what());
        return 1;
    }
}
