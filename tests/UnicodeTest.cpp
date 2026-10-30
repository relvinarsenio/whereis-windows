#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    ~UniqueHandle() {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

private:
    HANDLE handle_;
};

struct TemporaryDirectory {
    std::wstring root;
    std::wstring input;
    std::wstring output;

    ~TemporaryDirectory() {
        if (!input.empty()) DeleteFileW(input.c_str());
        if (!output.empty()) DeleteFileW(output.c_str());
        if (!root.empty()) RemoveDirectoryW(root.c_str());
    }
};

[[nodiscard]] std::wstring quoted(std::wstring_view value) {
    return L"\"" + std::wstring(value) + L"\"";
}

[[nodiscard]] int fail(const char* message, DWORD error = ERROR_SUCCESS) {
    if (error == ERROR_SUCCESS) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
    } else {
        std::fprintf(stderr, "[FAIL] %s (Win32 error %lu)\n", message, error);
    }
    return 1;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        return fail("expected the path to whereis.exe");
    }

    wchar_t temporary_name[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temporary_name) == 0) {
        return fail("GetTempPathW failed", GetLastError());
    }

    wchar_t temporary_file[MAX_PATH]{};
    if (GetTempFileNameW(temporary_name, L"wis", 0, temporary_file) == 0) {
        return fail("GetTempFileNameW failed", GetLastError());
    }

    if (!DeleteFileW(temporary_file)) {
        return fail("could not prepare the temporary directory", GetLastError());
    }
    if (!CreateDirectoryW(temporary_file, nullptr)) {
        return fail("could not create the temporary directory", GetLastError());
    }

    TemporaryDirectory temporary;
    temporary.root = temporary_file;
    temporary.input = temporary.root + L"\\whereis-invalid-";
    temporary.input.push_back(static_cast<wchar_t>(0xD800));
    temporary.input += L".exe";
    temporary.output = temporary.root + L"\\stdout.bin";

    {
        UniqueHandle fixture(CreateFileW(
            temporary.input.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        ));
        if (!fixture) {
            return fail("CreateFileW could not create a filename containing a lone surrogate", GetLastError());
        }
    }

    SECURITY_ATTRIBUTES security_attributes{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE
    };
    UniqueHandle output(CreateFileW(
        temporary.output.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (!output) {
        return fail("could not create the redirected output file", GetLastError());
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = output.get();
    startup.hStdOutput = output.get();
    startup.hStdError = output.get();

    const std::wstring executable = argv[1];
    const std::wstring query = temporary.input.substr(temporary.root.size() + 1,
        temporary.input.size() - temporary.root.size() - 1);
    std::wstring command = quoted(executable) + L" -b -B " + quoted(temporary.root) + L" -f " + quoted(query);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
        executable.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process_info
    )) {
        return fail("CreateProcessW could not start whereis", GetLastError());
    }

    UniqueHandle process(process_info.hProcess);
    UniqueHandle thread(process_info.hThread);
    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
        return fail("waiting for whereis failed", GetLastError());
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.get(), &exit_code)) {
        return fail("GetExitCodeProcess failed", GetLastError());
    }
    if (exit_code != 0) {
        return fail("whereis returned a non-zero exit status");
    }

    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(output.get(), beginning, nullptr, FILE_BEGIN)) {
        return fail("could not rewind redirected output", GetLastError());
    }

    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> buffer{};
    for (;;) {
        DWORD bytes_read = 0;
        if (!ReadFile(output.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
            return fail("could not read redirected output", GetLastError());
        }
        if (bytes_read == 0) break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + bytes_read);
    }

    constexpr std::array<std::uint8_t, 3> replacement{ 0xEF, 0xBF, 0xBD };
    if (std::search(bytes.begin(), bytes.end(), replacement.begin(), replacement.end()) == bytes.end()) {
        return fail("redirected output does not contain the UTF-8 replacement character U+FFFD");
    }
    if (bytes.empty() || bytes.back() != static_cast<std::uint8_t>('\n')) {
        return fail("redirected output does not end with LF");
    }
    if (bytes.size() >= 2 &&
        bytes[bytes.size() - 2] == static_cast<std::uint8_t>('\r') &&
        bytes.back() == static_cast<std::uint8_t>('\n')) {
        return fail("redirected output unexpectedly uses CRLF");
    }

    std::puts("[PASS] malformed UTF-16 filename is preserved with UTF-8 U+FFFD output");
    return 0;
}
