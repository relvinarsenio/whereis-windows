/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2026, Alfie Ardinata
 *
 * This file is part of the Windows whereis test suite.
 *
 * Shared test utilities for filesystem fixtures, environment variables,
 * and launching the whereis executable.
 *
 */

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <windows.h>

static_assert(sizeof(wchar_t) == sizeof(char16_t));

class TestSupport final {
public:
    class UniqueHandle final {
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
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

        void reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
            if (*this) { CloseHandle(handle_); }

            handle_ = replacement;
        }

    private:
        HANDLE handle_;
    };

    class TemporaryDirectory final {
    public:
        explicit TemporaryDirectory(std::wstring_view prefix = L"whereis-test-") {
            std::error_code error;
            const auto base = std::filesystem::temp_directory_path(error);

            if (error) { throw std::system_error { error, "cannot locate the temporary directory" }; }

            std::wstring directory_name { prefix };
            directory_name += std::to_wstring(GetCurrentProcessId());
            directory_name += L'-';
            directory_name += std::to_wstring(GetTickCount64());
            directory_name += L'-';

            for (unsigned int attempt = 0; attempt < 32; ++attempt) {
                const auto candidate = base / (directory_name + std::to_wstring(attempt));

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
        TemporaryDirectory(TemporaryDirectory&&)                 = delete;
        TemporaryDirectory& operator=(TemporaryDirectory&&)      = delete;

        ~TemporaryDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    private:
        std::filesystem::path path_;
    };

    class ScopedEnvironmentVariable final {
    public:
        ScopedEnvironmentVariable(std::wstring name, std::optional<std::wstring> value)
            : name_ { std::move(name) }
            , previous_ { get_environment_variable(name_) } {
            if (!SetEnvironmentVariableW(name_.c_str(), value ? value->c_str() : nullptr)) {
                throw_last_error("SetEnvironmentVariableW");
            }
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&)            = delete;
        ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable(ScopedEnvironmentVariable&&)                 = delete;
        ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable&&)      = delete;

        ~ScopedEnvironmentVariable() {
            SetEnvironmentVariableW(name_.c_str(), previous_ ? previous_->c_str() : nullptr);
        }

    private:
        std::wstring name_;
        std::optional<std::wstring> previous_;
    };

    struct ProcessResult {
        DWORD exit_code {};
        std::string stdout_output;
        std::string stderr_output;
    };

    struct ProcessOptions {
        std::filesystem::path working_directory;
        DWORD timeout_ms = 20'000;
    };

    [[nodiscard]] static UniqueHandle open_file(const std::filesystem::path& path, DWORD access, DWORD share_mode,
        DWORD creation_disposition, bool inheritable = false) {
        SECURITY_ATTRIBUTES security_attributes {};
        security_attributes.nLength        = static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES));
        security_attributes.bInheritHandle = inheritable ? TRUE : FALSE;

        UniqueHandle file { CreateFileW(path.c_str(), access, share_mode, &security_attributes, creation_disposition,
            FILE_ATTRIBUTE_NORMAL, nullptr) };

        if (!file) { throw_last_error("CreateFileW"); }

        return file;
    }

    static void write_file(const std::filesystem::path& path, std::string_view contents = {}) {
        std::ofstream file { path, std::ios::binary };

        if (!file) { throw std::runtime_error { "cannot create a test file" }; }

        if (!contents.empty()) { file.write(contents.data(), static_cast<std::streamsize>(contents.size())); }

        file.close();

        if (!file) { throw std::runtime_error { "cannot write a test file" }; }
    }

    [[nodiscard]] static std::optional<std::wstring> get_environment_variable(std::wstring_view name) {
        const std::wstring key { name };

        for (;;) {
            SetLastError(ERROR_SUCCESS);
            const DWORD required = GetEnvironmentVariableW(key.c_str(), nullptr, 0);

            if (required == 0) {
                const DWORD error = GetLastError();

                if (error == ERROR_ENVVAR_NOT_FOUND) { return std::nullopt; }
                if (error == ERROR_SUCCESS) { return std::wstring {}; }

                throw_windows_error("GetEnvironmentVariableW", error);
            }

            std::wstring value(required, L'\0');

            SetLastError(ERROR_SUCCESS);
            const DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), required);

            if (written == 0) {
                const DWORD error = GetLastError();

                if (error == ERROR_ENVVAR_NOT_FOUND) { return std::nullopt; }
                if (error == ERROR_SUCCESS) { return std::wstring {}; }

                throw_windows_error("GetEnvironmentVariableW", error);
            }

            if (written >= required) { continue; }

            value.resize(written);
            return value;
        }
    }

    [[nodiscard]] static std::filesystem::path whereis_executable() {
        const auto value = get_environment_variable(L"WHEREIS_EXE");

        if (!value || value->empty()) { throw std::runtime_error { "WHEREIS_EXE is not set." }; }

        return std::filesystem::path { *value };
    }

    [[nodiscard]] static ProcessResult run_process(const std::filesystem::path& executable,
        std::span<const std::wstring_view> arguments, const ProcessOptions& options = {}) {
        TemporaryDirectory capture_directory { L"whereis-process-" };

        auto stdout_file = open_file(capture_directory.path() / L"stdout.bin", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, true);
        auto stderr_file = open_file(capture_directory.path() / L"stderr.bin", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, true);
        auto stdin_file  = open_file(
            std::filesystem::path { L"NUL" }, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, true);

        STARTUPINFOW startup {};
        startup.cb         = static_cast<DWORD>(sizeof(STARTUPINFOW));
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdInput  = stdin_file.get();
        startup.hStdOutput = stdout_file.get();
        startup.hStdError  = stderr_file.get();

        std::wstring command_line;
        append_quoted_argument(command_line, executable.native());

        for (const auto argument : arguments) {
            append_quoted_argument(command_line, argument);
        }

        PROCESS_INFORMATION process_info {};
        const wchar_t* working_directory
            = options.working_directory.empty() ? nullptr : options.working_directory.c_str();

        if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                working_directory, &startup, &process_info)) {
            throw_last_error("CreateProcessW");
        }

        UniqueHandle process { process_info.hProcess };
        UniqueHandle thread { process_info.hThread };

        const DWORD wait_result = WaitForSingleObject(process.get(), options.timeout_ms);

        if (wait_result == WAIT_TIMEOUT) {
            if (TerminateProcess(process.get(), ERROR_TIMEOUT)) {
                if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
                    throw_last_error("WaitForSingleObject after TerminateProcess");
                }

                throw std::runtime_error { "process did not exit before the test timeout" };
            }

            const DWORD terminate_error = GetLastError();

            if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) {
                throw_windows_error("TerminateProcess", terminate_error);
            }
        } else if (wait_result != WAIT_OBJECT_0) {
            const DWORD wait_error = GetLastError();

            TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
            WaitForSingleObject(process.get(), INFINITE);

            throw_windows_error("WaitForSingleObject", wait_error);
        }

        DWORD exit_code = 0;

        if (!GetExitCodeProcess(process.get(), &exit_code)) { throw_last_error("GetExitCodeProcess"); }

        if (!FlushFileBuffers(stdout_file.get())) { throw_last_error("FlushFileBuffers(stdout)"); }
        if (!FlushFileBuffers(stderr_file.get())) { throw_last_error("FlushFileBuffers(stderr)"); }

        return ProcessResult {
            .exit_code     = exit_code,
            .stdout_output = read_all(stdout_file.get()),
            .stderr_output = read_all(stderr_file.get()),
        };
    }

    [[nodiscard]] static ProcessResult run_process(const std::filesystem::path& executable,
        std::initializer_list<std::wstring_view> arguments, const ProcessOptions& options = {}) {
        return run_process(
            executable, std::span<const std::wstring_view> { arguments.begin(), arguments.size() }, options);
    }

    [[nodiscard]] static ProcessResult run_whereis(
        std::span<const std::wstring_view> arguments, const ProcessOptions& options = {}) {
        return run_process(whereis_executable(), arguments, options);
    }

    [[nodiscard]] static ProcessResult run_whereis(
        std::initializer_list<std::wstring_view> arguments, const ProcessOptions& options = {}) {
        return run_process(whereis_executable(), arguments, options);
    }

private:
    TestSupport() = delete;

    [[noreturn]] static void throw_windows_error(std::string_view operation, DWORD error) {
        throw std::system_error { static_cast<int>(error), std::system_category(), std::string { operation } };
    }

    [[noreturn]] static void throw_last_error(std::string_view operation) {
        throw_windows_error(operation, GetLastError());
    }

    static void append_quoted_argument(std::wstring& command_line, std::wstring_view argument) {
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

    [[nodiscard]] static std::string read_all(HANDLE file) {
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
};
