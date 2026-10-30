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

#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <climits>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace whereis {

	constexpr int usage_error = 64;
	constexpr int data_error = 65;
	constexpr unsigned no_binary = 1u << 0;
	constexpr unsigned no_manual = 1u << 1;
	constexpr unsigned no_source = 1u << 2;
	constexpr unsigned max_source_depth = 8;

	constexpr std::array default_path_extensions{
		std::wstring_view{ L".COM" }, std::wstring_view{ L".EXE" },
		std::wstring_view{ L".BAT" }, std::wstring_view{ L".CMD" },
		std::wstring_view{ L".VBS" }, std::wstring_view{ L".VBE" },
		std::wstring_view{ L".JS" }, std::wstring_view{ L".JSE" },
		std::wstring_view{ L".WSF" }, std::wstring_view{ L".WSH" },
		std::wstring_view{ L".MSC" }
	};

	constexpr std::array manual_sections{
		std::wstring_view{ L"1" }, std::wstring_view{ L"2" },
		std::wstring_view{ L"3" }, std::wstring_view{ L"4" },
		std::wstring_view{ L"5" }, std::wstring_view{ L"6" },
		std::wstring_view{ L"7" }, std::wstring_view{ L"8" },
		std::wstring_view{ L"9" }, std::wstring_view{ L"1p" },
		std::wstring_view{ L"3p" }
	};

	constexpr std::array manual_compression_suffixes{
		std::wstring_view{ L"" }, std::wstring_view{ L".gz" },
		std::wstring_view{ L".bz2" }, std::wstring_view{ L".xz" },
		std::wstring_view{ L".zst" }, std::wstring_view{ L".html" },
		std::wstring_view{ L".htm" }
	};

	constexpr std::array document_extensions{
		std::wstring_view{ L".md" }, std::wstring_view{ L".markdown" },
		std::wstring_view{ L".html" }, std::wstring_view{ L".htm" },
		std::wstring_view{ L".pdf" }, std::wstring_view{ L".txt" },
		std::wstring_view{ L".rst" }, std::wstring_view{ L".adoc" }
	};

	struct Options {
		bool all{};
		bool binaries{};
		bool manuals{};
		bool quiet{};
		bool sources{};
		bool unusual{};
		bool no_recursive_sources{};
		bool help{};
		bool implicit_all_categories{};

		bool custom_binary_dirs{};
		bool custom_manual_dirs{};
		bool custom_source_dirs{};

		std::vector<fs::path> binary_dirs;
		std::vector<fs::path> manual_dirs;
		std::vector<fs::path> source_dirs;
		std::vector<std::wstring> queries;
	};

	struct SearchRequest {
		std::wstring_view name;
		bool all{};
		bool skip_recursive{};
	};

	struct SearchResult {
		std::vector<fs::path> binaries;
		std::vector<fs::path> manuals;
		std::vector<fs::path> sources;
		unsigned missing{};
	};

	template<typename T>
	concept SearchBackend = requires(const T & backend, const SearchRequest & request) {
		{ backend.find(request) } -> std::same_as<std::vector<fs::path>>;
	};

	[[nodiscard]] std::wstring_view trim_view(std::wstring_view value) noexcept {
		constexpr std::wstring_view whitespace{ L" \t\r\n" };
		const auto first = value.find_first_not_of(whitespace);
		if (first == std::wstring_view::npos) return {};
		const auto last = value.find_last_not_of(whitespace);
		return value.substr(first, last - first + 1);
	}

	[[nodiscard]] std::wstring unquote(std::wstring value) {
		value = std::wstring{ trim_view(value) };
		if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
			value = value.substr(1, value.size() - 2);
		}
		return value;
	}

	[[nodiscard]] bool equal_ordinal(
		std::wstring_view left,
		std::wstring_view right,
		bool ignore_case = true) noexcept {
		if (left.size() > static_cast<std::size_t>(INT_MAX) ||
			right.size() > static_cast<std::size_t>(INT_MAX)) return false;

		return CompareStringOrdinal(
			left.data(), static_cast<int>(left.size()),
			right.data(), static_cast<int>(right.size()),
			ignore_case ? TRUE : FALSE) == CSTR_EQUAL;
	}

	[[nodiscard]] std::optional<std::wstring> environment_value(std::wstring_view name) {
		const std::wstring key{ name };
		const DWORD required = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
		if (required == 0) return std::nullopt;

		std::wstring value(required, L'\0');
		const DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), required);
		if (written == 0 || written >= required) return std::nullopt;

		value.resize(written);
		return value;
	}

	[[nodiscard]] std::wstring expand_environment(std::wstring_view value) {
		const std::wstring source{ value };
		const DWORD required = ExpandEnvironmentStringsW(source.c_str(), nullptr, 0);
		if (required == 0) return source;

		std::wstring expanded(required, L'\0');
		const DWORD written = ExpandEnvironmentStringsW(
			source.c_str(), expanded.data(), required);
		if (written == 0 || written > required) return source;

		expanded.resize(written - 1);
		return expanded;
	}

	[[nodiscard]] fs::path normalize_path(std::wstring_view raw) {
		auto value = unquote(std::wstring{ raw });
		value = expand_environment(value);

		if (value == L"~" || value.starts_with(L"~\\") || value.starts_with(L"~/")) {
			if (const auto profile = environment_value(L"USERPROFILE")) {
				value.replace(0, 1, *profile);
			}
		}

		fs::path path{ value };
		std::error_code error;
		if (path.is_relative()) {
			auto absolute = fs::absolute(path, error);
			if (!error) path = std::move(absolute);
		}

		path.make_preferred();
		path = path.lexically_normal();
		if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
		return path;
	}

	[[nodiscard]] fs::path without_trailing_separator(fs::path path) {
		path = path.lexically_normal();
		if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
		return path;
	}

	[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
		const auto a = without_trailing_separator(left).native();
		const auto b = without_trailing_separator(right).native();
		return equal_ordinal(a, b);
	}

	void append_unique_path(std::vector<fs::path>& paths, const fs::path& candidate) {
		const bool duplicate = std::ranges::any_of(
			paths, [&candidate](const fs::path& current) { return same_path(current, candidate); });
		if (!duplicate) paths.push_back(candidate);
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
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	class DirectorySet {
	public:
		void add(std::wstring_view value) {
			add(normalize_path(value));
		}

		void add(const fs::path& path) {
			if (!path.empty()) append_unique_path(paths_, path);
		}

		void add_list(std::wstring_view value) {
			for (const auto& segment : value | std::views::split(L';')) {
				const std::wstring part{ segment.begin(), segment.end() };
				if (!trim_view(part).empty()) add(std::wstring_view{ part });
			}
		}

		void add_existing(const fs::path& path) {
			if (is_directory_path(path)) add(path);
		}

		[[nodiscard]] bool empty() const noexcept { return paths_.empty(); }
		[[nodiscard]] std::span<const fs::path> values() const noexcept { return paths_; }

	private:
		std::vector<fs::path> paths_;
	};

	[[nodiscard]] bool is_simple_flag(wchar_t flag) noexcept {
		constexpr std::wstring_view flags{ L"abmqsuxh" };
		return flags.find(flag) != std::wstring_view::npos;
	}

	[[nodiscard]] bool is_flag_cluster(std::wstring_view argument) noexcept {
		if (argument.size() < 2 || argument.front() != L'-' || argument == L"--") return false;

		auto flags = argument.substr(1);
		while (!flags.empty()) {
			const wchar_t flag = flags.front();
			flags.remove_prefix(1);
			if (is_simple_flag(flag)) continue;

			// -B/-M/-S consume a directory list; -f terminates one. They must
			// be the final character in a cluster, e.g. -aB, -abf.
			return flags.empty() &&
				(flag == L'B' || flag == L'M' || flag == L'S' || flag == L'f');
		}
		return true;
	}

	[[nodiscard]] bool is_directory_switch(std::wstring_view argument) noexcept {
		return argument == L"-B" || argument == L"-M" || argument == L"-S";
	}

	[[nodiscard]] wchar_t terminal_directory_switch(std::wstring_view argument) noexcept {
		if (!is_flag_cluster(argument)) return L'\0';
		const wchar_t last = argument.back();
		return last == L'B' || last == L'M' || last == L'S' ? last : L'\0';
	}

	void apply_simple_flag(Options& options, wchar_t flag) {
		switch (flag) {
		case L'a': options.all = true; break;
		case L'b': options.binaries = true; break;
		case L'm': options.manuals = true; break;
		case L'q': options.quiet = true; break;
		case L's': options.sources = true; break;
		case L'u': options.unusual = true; break;
		case L'x': options.no_recursive_sources = true; break;
		case L'h': options.help = true; break;
		}
	}

	[[nodiscard]] std::expected<Options, std::wstring> parse_arguments(
		std::span<const std::wstring_view> arguments) {
		Options options;
		bool end_of_options{};

		auto argument = arguments.begin();
		const auto end = arguments.end();

		while (argument != end) {
			const auto current = *argument++;

			if (end_of_options) {
				options.queries.emplace_back(current);
				continue;
			}

			if (current == L"--") {
				end_of_options = true;
				continue;
			}

			if (current == L"-f") {
				for (const auto query : std::ranges::subrange(argument, end)) {
					options.queries.emplace_back(query);
				}
				argument = end;
				break;
			}

			if (current == L"--help") {
				options.help = true;
				continue;
			}

			if (!current.starts_with(L'-') || current.size() == 1) {
				options.queries.emplace_back(current);
				continue;
			}

			if (!is_flag_cluster(current)) {
				return std::unexpected(L"Unknown option: " + std::wstring{ current });
			}

			auto flags = current.substr(1);

			while (!flags.empty()) {
				const wchar_t flag = flags.front();
				flags.remove_prefix(1);

				if (is_simple_flag(flag)) {
					apply_simple_flag(options, flag);
					continue;
				}

				if (flag == L'f') {
					for (const auto query : std::ranges::subrange(argument, end)) {
						options.queries.emplace_back(query);
					}
					argument = end;
					break;
				}

				std::vector<fs::path>* target{};
				bool* custom{};

				if (flag == L'B') {
					target = &options.binary_dirs;
					custom = &options.custom_binary_dirs;
				}
				else if (flag == L'M') {
					target = &options.manual_dirs;
					custom = &options.custom_manual_dirs;
				}
				else if (flag == L'S') {
					target = &options.source_dirs;
					custom = &options.custom_source_dirs;
				}

				if (target == nullptr || !flags.empty()) {
					return std::unexpected(
						L"Directory options -B, -M and -S must end an option cluster.");
				}

				DirectorySet parsed_directories;
				bool received_directory{};

				while (argument != end) {
					const auto next = *argument;
					if (next == L"--" || next == L"-f" || is_directory_switch(next) ||
						terminal_directory_switch(next) != L'\0' ||
						(is_flag_cluster(next) && next.back() == L'f')) {
						break;
					}

					parsed_directories.add_list(next);
					received_directory = true;
					++argument;
				}

				if (!received_directory || parsed_directories.empty()) {
					return std::unexpected(
						L"Option -" + std::wstring(1, flag) +
						L" requires at least one directory before -f or another directory option.");
				}

				for (const auto& path : parsed_directories.values()) {
					append_unique_path(*target, path);
				}
				*custom = true;
				break;
			}
		}

		if (options.help) return options;
		if (options.queries.empty()) {
			return std::unexpected(L"No program names were specified.");
		}

		if (!options.binaries && !options.manuals && !options.sources) {
			options.binaries = true;
			options.manuals = true;
			options.sources = true;
			options.implicit_all_categories = true;
		}

		return options;
	}

	void add_environment_directories(DirectorySet& target, std::wstring_view variable) {
		if (const auto value = environment_value(variable)) target.add_list(*value);
	}

	using WindowsDirectoryGetter = UINT(WINAPI*)(LPWSTR, UINT);

	void add_windows_directory(DirectorySet& target, WindowsDirectoryGetter getter) {
		std::wstring buffer(32768, L'\0');
		const UINT written = getter(buffer.data(), static_cast<UINT>(buffer.size()));
		if (written == 0 || written >= static_cast<UINT>(buffer.size())) return;
		buffer.resize(written);
		target.add(std::wstring_view{ buffer });
	}

	[[nodiscard]] fs::path program_files_path(std::wstring_view variable, std::wstring_view suffix) {
		const auto root = environment_value(variable);
		if (!root) return {};
		return normalize_path((fs::path{ *root } / std::wstring{ suffix }).native());
	}

	void configure_binary_directories(Options& options) {
		if (options.custom_binary_dirs) return;

		DirectorySet directories;
		add_environment_directories(directories, L"PATH");
		add_windows_directory(directories, GetSystemDirectoryW);
		add_windows_directory(directories, GetWindowsDirectoryW);

		options.binary_dirs.assign(directories.values().begin(), directories.values().end());
	}

	void configure_manual_directories(Options& options) {
		if (options.custom_manual_dirs) return;

		DirectorySet directories;
		add_environment_directories(directories, L"MANPATH");

		if (directories.empty()) {
			DirectorySet path_directories;
			add_environment_directories(path_directories, L"PATH");

			for (const auto& path : path_directories.values()) {
				if (!equal_ordinal(path.filename().native(), L"bin")) continue;
				directories.add_existing(path.parent_path() / L"share" / L"man");
				directories.add_existing(path.parent_path() / L"usr" / L"share" / L"man");
				directories.add_existing(path.parent_path().parent_path() / L"usr" / L"share" / L"man");
			}

			for (const auto variable : { std::wstring_view{ L"MSYS2_ROOT" },
										std::wstring_view{ L"CYGWIN_ROOT" },
										std::wstring_view{ L"GIT_INSTALL_ROOT" } }) {
				if (const auto root = environment_value(variable)) {
					directories.add_existing(fs::path{ *root } / L"usr" / L"share" / L"man");
				}
			}

			directories.add_existing(program_files_path(L"ProgramFiles", L"Git\\usr\\share\\man"));
			directories.add_existing(program_files_path(L"ProgramFiles(x86)", L"Git\\usr\\share\\man"));
		}

		options.manual_dirs.assign(directories.values().begin(), directories.values().end());
	}

	void configure_source_directories(Options& options) {
		if (options.custom_source_dirs) return;

		DirectorySet directories;
		add_environment_directories(directories, L"SRCPATH");
		add_environment_directories(directories, L"PORTSDIR");

		if (const auto vcpkg = environment_value(L"VCPKG_ROOT")) {
			directories.add_existing(fs::path{ *vcpkg } / L"ports");
		}

		if (const auto profile = environment_value(L"USERPROFILE")) {
			const fs::path home{ *profile };
			directories.add_existing(home / L"source");
			directories.add_existing(home / L"source" / L"repos");
			directories.add_existing(home / L"src");
			directories.add_existing(home / L"Documents" / L"source");
			directories.add_existing(home / L"Documents" / L"GitHub");
			directories.add_existing(home / L"Documents" / L"Visual Studio 2022" / L"Projects");
			directories.add_existing(home / L"Documents" / L"Visual Studio 2026" / L"Projects");
		}

		if (const auto system_drive = environment_value(L"SystemDrive")) {
			directories.add_existing(fs::path{ *system_drive } / L"src");
			directories.add_existing(fs::path{ *system_drive } / L"source");
		}

		options.source_dirs.assign(directories.values().begin(), directories.values().end());
	}

	void configure_directories(Options& options) {
		if (options.binaries) configure_binary_directories(options);
		if (options.manuals) configure_manual_directories(options);
		if (options.sources) configure_source_directories(options);

		if (options.implicit_all_categories) {
			if (options.binary_dirs.empty()) options.binaries = false;
			if (options.manual_dirs.empty()) options.manuals = false;
			if (options.source_dirs.empty()) options.sources = false;
		}
	}

	[[nodiscard]] std::wstring normalized_query(std::wstring_view argument) {
		fs::path input{ argument };
		auto name = input.filename().native();

		constexpr std::array compression_suffixes{
			std::wstring_view{ L".bz2" }, std::wstring_view{ L".zst" },
			std::wstring_view{ L".lzma" }, std::wstring_view{ L".gz" },
			std::wstring_view{ L".xz" }, std::wstring_view{ L".Z" },
			std::wstring_view{ L".z" }
		};

		for (const auto suffix : compression_suffixes) {
			if (name.size() > suffix.size() &&
				equal_ordinal(std::wstring_view{ name }.substr(name.size() - suffix.size()), suffix)) {
				name.resize(name.size() - suffix.size());
				break;
			}
		}

		return name;
	}

	[[nodiscard]] std::vector<std::wstring> path_extensions() {
		std::vector<std::wstring> result;
		const auto value = environment_value(L"PATHEXT");
		if (!value) {
			for (const auto extension : default_path_extensions) result.emplace_back(extension);
			return result;
		}

		for (const auto& segment : *value | std::views::split(L';')) {
			auto extension = unquote(std::wstring{ segment.begin(), segment.end() });
			if (extension.empty()) continue;
			if (extension.front() != L'.') extension.insert(extension.begin(), L'.');
			if (std::ranges::none_of(result, [&extension](const std::wstring& current) {
				return equal_ordinal(current, extension);
				})) {
				result.push_back(std::move(extension));
			}
		}

		return result;
	}

	[[nodiscard]] fs::path canonical_case(const fs::path& path) {
		std::wstring buffer(MAX_PATH, L'\0');
		DWORD written = GetLongPathNameW(
			path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));

		if (written == 0) return path;
		if (written >= buffer.size()) {
			buffer.resize(written);
			written = GetLongPathNameW(
				path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
		}

		if (written == 0 || written >= buffer.size()) return path;
		buffer.resize(written);
		return fs::path{ buffer };
	}

	class RegistryKey final {
	public:
		explicit RegistryKey(HKEY key) noexcept : key_{ key } {}

		~RegistryKey() noexcept {
			if (key_ != nullptr) RegCloseKey(key_);
		}

		RegistryKey(const RegistryKey&) = delete;
		RegistryKey& operator=(const RegistryKey&) = delete;

		[[nodiscard]] HKEY get() const noexcept { return key_; }

	private:
		HKEY key_{};
	};

	[[nodiscard]] std::optional<std::wstring> registry_app_path(std::wstring_view executable_name) {
		constexpr std::wstring_view key_prefix{
			L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" };
		const std::wstring key_name = std::wstring{ key_prefix } + std::wstring{ executable_name };

		const std::array registry_roots{ HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
		constexpr std::array<REGSAM, 2> registry_views{
			static_cast<REGSAM>(KEY_WOW64_64KEY), static_cast<REGSAM>(KEY_WOW64_32KEY) };

		for (const auto root : registry_roots) {
			for (const REGSAM view : registry_views) {
				HKEY raw_key{};
				if (RegOpenKeyExW(
					root, key_name.c_str(), 0, KEY_QUERY_VALUE | view, &raw_key) != ERROR_SUCCESS) {
					continue;
				}
				const RegistryKey key{ raw_key };

				DWORD type{};
				DWORD bytes{};
				const LONG size_status = RegQueryValueExW(
					key.get(), nullptr, nullptr, &type, nullptr, &bytes);
				if (size_status != ERROR_SUCCESS || bytes < sizeof(wchar_t) ||
					bytes > 65536 || bytes % sizeof(wchar_t) != 0) {
					continue;
				}

				std::vector<BYTE> storage(bytes + sizeof(wchar_t));
				const LONG read_status = RegQueryValueExW(
					key.get(), nullptr, nullptr, &type, storage.data(), &bytes);
				if (read_status != ERROR_SUCCESS ||
					(type != REG_SZ && type != REG_EXPAND_SZ)) continue;

				const auto* characters = reinterpret_cast<const wchar_t*>(storage.data());
				const auto count = bytes / sizeof(wchar_t);
				std::wstring value{ characters, count };
				while (!value.empty() && value.back() == L'\0') value.pop_back();
				if (type == REG_EXPAND_SZ) value = expand_environment(value);
				value = unquote(std::move(value));
				if (!value.empty()) return value;
			}
		}

		return std::nullopt;
	}

	class BinarySearcher {
	public:
		explicit BinarySearcher(std::span<const fs::path> directories)
			: directories_{ directories.begin(), directories.end() },
			extensions_{ path_extensions() } {}

		[[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
			std::vector<fs::path> matches;
			const fs::path requested{ request.name };
			std::vector<fs::path> file_names;

			if (requested.has_extension() &&
				is_executable_extension(requested.extension().native())) {
				file_names.push_back(requested);
			}
			else {
				for (const auto& extension : extensions_) {
					file_names.emplace_back(std::wstring{ request.name } + extension);
				}
			}

			// Resolve exactly like a PATH lookup: directory order first, then PATHEXT.
			for (const auto& directory : directories_) {
				for (const auto& file_name : file_names) {
					const auto candidate = directory / file_name;
					if (!is_executable_file(candidate)) continue;
					append_unique_path(matches, canonical_case(candidate));
					if (!request.all) return matches;
				}
			}

			// App Paths is a ShellExecute/Run-style fallback, never ahead of PATH.
			for (const auto& file_name : file_names) {
				const auto app_path = registry_app_path(file_name.filename().native());
				if (!app_path) continue;

				const auto candidate = normalize_path(*app_path);
				if (!is_executable_file(candidate)) continue;
				append_unique_path(matches, canonical_case(candidate));
				if (!request.all) return matches;
			}

			return matches;
		}

	private:
		[[nodiscard]] bool is_executable_extension(std::wstring_view extension) const noexcept {
			constexpr std::array native_extensions{
				std::wstring_view{ L".exe" }, std::wstring_view{ L".com" },
				std::wstring_view{ L".bat" }, std::wstring_view{ L".cmd" }
			};
			if (std::ranges::any_of(native_extensions, [extension](std::wstring_view current) {
				return equal_ordinal(current, extension);
				})) return true;

			return std::ranges::any_of(extensions_, [extension](const std::wstring& current) {
				return equal_ordinal(current, extension);
				});
		}

		[[nodiscard]] bool is_executable_file(const fs::path& candidate) const noexcept {
			return is_file_path(candidate) &&
				is_executable_extension(candidate.extension().native());
		}

		std::vector<fs::path> directories_;
		std::vector<std::wstring> extensions_;
	};

	class ManualSearcher {
	public:
		explicit ManualSearcher(std::span<const fs::path> directories)
			: directories_{ directories.begin(), directories.end() } {}

		[[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
			std::vector<fs::path> matches;

			for (const auto& root : directories_) {
				for (const auto& layout : manual_layouts(root)) {
					for (const auto section : manual_sections) {
						const auto stem = std::wstring{ request.name } + L"." + std::wstring{ section };
						auto section_name = std::wstring{ L"man" };
						section_name.append(section);
						const auto section_directory = layout / section_name;
						for (const auto suffix : manual_compression_suffixes) {
							const auto page = stem + std::wstring{ suffix };
							add_if_file(matches, section_directory / page);
							add_if_file(matches, layout / page);
							if (!request.all && !matches.empty()) return matches;
						}
					}

					for (const auto extension : document_extensions) {
						add_if_file(matches,
							layout / (std::wstring{ request.name } + std::wstring{ extension }));
						if (!request.all && !matches.empty()) return matches;
					}
				}
			}

			return matches;
		}

	private:
		[[nodiscard]] static std::vector<fs::path> manual_layouts(const fs::path& root) {
			return {
				root,
				root / L"man",
				root / L"doc",
				root / L"docs",
				root / L"share" / L"man"
			};
		}

		static void add_if_file(
			std::vector<fs::path>& matches,
			const fs::path& candidate) {
			if (!is_regular_file_path(candidate)) return;
			append_unique_path(matches, candidate);
		}

		std::vector<fs::path> directories_;
	};

	class SourceSearcher {
	public:
		explicit SourceSearcher(std::span<const fs::path> directories)
			: directories_{ directories.begin(), directories.end() } {}

		[[nodiscard]] std::vector<fs::path> find(const SearchRequest& request) const {
			std::vector<fs::path> matches;

			for (const auto& root : directories_) {
				const auto direct = root / request.name;
				if (!is_directory_path(direct)) continue;
				append_unique_path(matches, direct);
				if (!request.all) return matches;
			}

			if (request.skip_recursive || directories_.empty()) return matches;

			for (const auto& root : directories_) {
				search_tree(root, request.name, request.all, matches);
				if (!request.all && !matches.empty()) return matches;
			}

			return matches;
		}

	private:
		static void search_tree(
			const fs::path& root,
			std::wstring_view name,
			bool all,
			std::vector<fs::path>& matches) {
			std::error_code error;
			for (auto current = fs::recursive_directory_iterator{
					 root, fs::directory_options::skip_permission_denied, error },
					 end = fs::recursive_directory_iterator{};
					 !error && current != end;
				current.increment(error)) {
				const auto entry_path = current->path();
				const auto depth = current.depth();
				std::error_code type_error;
				const bool directory = current->is_directory(type_error);

				if (depth >= static_cast<int>(max_source_depth) ||
					should_prune(entry_path.filename())) {
					current.disable_recursion_pending();
				}

				if (!type_error && directory && equal_ordinal(entry_path.filename().native(), name)) {
					append_unique_path(matches, entry_path);
					if (!all) return;
				}
			}
		}

		[[nodiscard]] static bool should_prune(const fs::path& directory) noexcept {
			const auto& name = directory.native();
			constexpr std::array ignored{
				std::wstring_view{ L".git" }, std::wstring_view{ L".hg" },
				std::wstring_view{ L".svn" }, std::wstring_view{ L".vs" },
				std::wstring_view{ L".cache" }, std::wstring_view{ L"node_modules" },
				std::wstring_view{ L"packages" }, std::wstring_view{ L"build" },
				std::wstring_view{ L"out" }, std::wstring_view{ L"obj" }
			};
			return std::ranges::any_of(ignored, [&name](std::wstring_view item) {
				return equal_ordinal(name, item);
				});
		}

		std::vector<fs::path> directories_;
	};

	static_assert(SearchBackend<BinarySearcher>);
	static_assert(SearchBackend<ManualSearcher>);
	static_assert(SearchBackend<SourceSearcher>);

	[[nodiscard]] std::wstring quote_path(const fs::path& path) {
		auto value = path.native();
		if (value.find_first_of(L" \t") == std::wstring::npos) return value;

		std::wstring quoted;
		quoted.reserve(value.size() + 2);
		quoted.push_back(L'"');
		quoted.append(value);
		quoted.push_back(L'"');
		return quoted;
	}

	[[nodiscard]] std::wstring make_output_line(
		std::wstring_view name,
		const SearchResult& result,
		bool quiet) {
		std::wstring line;
		bool has_output{};

		if (!quiet) {
			line.append(name);
			line.push_back(L':');
			has_output = true;
		}

		const auto append_paths = [&line, &has_output](const std::vector<fs::path>& paths) {
			for (const auto& path : paths) {
				if (has_output) line.push_back(L' ');
				line.append(quote_path(path));
				has_output = true;
			}
			};

		append_paths(result.binaries);
		append_paths(result.manuals);
		append_paths(result.sources);

		if (has_output) line.push_back(L'\n');
		return line;
	}

	class ConsoleOutput {
	public:
		static void stdout_text(std::wstring_view text) noexcept {
			write(GetStdHandle(STD_OUTPUT_HANDLE), text);
		}

		static void stderr_text(std::wstring_view text) noexcept {
			write(GetStdHandle(STD_ERROR_HANDLE), text);
		}

	private:
		static void write(HANDLE handle, std::wstring_view text) noexcept {
			if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty()) return;

			DWORD console_mode{};
			if (GetConsoleMode(handle, &console_mode)) {
				std::wstring console_text;
				if (text.find(L'\n') != std::wstring_view::npos) {
					console_text.reserve(text.size() + 8);
					wchar_t previous{};
					for (const wchar_t character : text) {
						if (character == L'\n' && previous != L'\r') {
							console_text.push_back(L'\r');
						}
						console_text.push_back(character);
						previous = character;
					}
					text = console_text;
				}

				auto remaining = text;
				while (!remaining.empty()) {
					const DWORD chunk = static_cast<DWORD>(
						std::min<std::size_t>(remaining.size(), 32768));
					DWORD written{};
					if (!WriteConsoleW(
						handle, remaining.data(), chunk, &written, nullptr) || written == 0) {
						return;
					}
					remaining.remove_prefix(written);
				}
				return;
			}

			const int source_length = static_cast<int>(std::min<std::size_t>(
				text.size(), static_cast<std::size_t>(INT_MAX)));
			// Flags 0 permit replacement of malformed UTF-16 instead of dropping
			// the entire output line when a filename contains a lone surrogate.
			const int required = WideCharToMultiByte(
				CP_UTF8, 0, text.data(), source_length,
				nullptr, 0, nullptr, nullptr);
			if (required <= 0) return;

			std::string utf8(static_cast<std::size_t>(required), '\0');
			const int converted = WideCharToMultiByte(
				CP_UTF8, 0, text.data(), source_length,
				utf8.data(), required, nullptr, nullptr);
			if (converted <= 0) return;

			auto remaining = std::string_view{ utf8 };
			while (!remaining.empty()) {
				const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
					remaining.size(), static_cast<std::size_t>(MAXDWORD)));
				DWORD written{};
				if (!WriteFile(
					handle, remaining.data(), chunk, &written, nullptr) || written == 0) {
					return;
				}
				remaining.remove_prefix(written);
			}
		}
	};

	void print_usage(bool to_stderr = false) {
		constexpr std::wstring_view help_text =
			L"whereis-windows - locate executables, documentation, and source directories\n"
			L"Usage: whereis [-abmqsuxh] [-B dir ...] [-M dir ...] [-S dir ... -f] name ...\n"
			L"       whereis --help\n\n"
			L"  -a  Find all matches instead of the first match in each category\n"
			L"  -b  Search for executable files (PATH, PATHEXT, and App Paths)\n"
			L"  -m  Search manual pages and documentation (MANPATH)\n"
			L"  -s  Search source directories (SRCPATH and common Windows source roots)\n"
			L"  -B  Override executable search directories\n"
			L"  -M  Override manual/documentation search directories\n"
			L"  -S  Override source search directories\n"
			L"  -f  End directory lists and begin program names\n"
			L"  -q  Do not print the program name label\n"
			L"  -u  Print only names missing at least one requested category\n"
			L"  -x  Skip recursive source discovery\n"
			L"  -h, --help  Show this help\n"
			L"  --          Stop option processing; treat remaining arguments as program names\n\n"
			L"Directory lists use Windows' semicolon separator. Quote paths containing spaces.\n"
			L"When no category is selected, -bms is assumed.\n";
		if (to_stderr) ConsoleOutput::stderr_text(help_text);
		else ConsoleOutput::stdout_text(help_text);
	}

	template<SearchBackend Binary, SearchBackend Manual, SearchBackend Source>
	[[nodiscard]] SearchResult search_one(
		std::wstring_view name,
		const Options& options,
		const Binary& binaries,
		const Manual& manuals,
		const Source& sources) {
		const SearchRequest request{
			.name = name,
			.all = options.all,
			.skip_recursive = options.no_recursive_sources
		};
		SearchResult result;

		if (options.binaries) {
			result.binaries = binaries.find(request);
			if (result.binaries.empty()) result.missing |= no_binary;
		}

		if (options.manuals) {
			result.manuals = manuals.find(request);
			if (result.manuals.empty()) result.missing |= no_manual;
		}

		if (options.sources) {
			result.sources = sources.find(request);
			if (result.sources.empty()) result.missing |= no_source;
		}

		return result;
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
			ConsoleOutput::stderr_text(
				L"whereis: no manual directories to search. Set MANPATH or use -M.\n");
			return data_error;
		}
		if (options.sources && options.source_dirs.empty()) {
			ConsoleOutput::stderr_text(
				L"whereis: no source directories to search. Set SRCPATH or use -S.\n");
			return data_error;
		}

		const BinarySearcher binaries{ options.binary_dirs };
		const ManualSearcher manuals{ options.manual_dirs };
		const SourceSearcher sources{ options.source_dirs };

		for (const auto& requested : options.queries) {
			const auto name = normalized_query(requested);
			if (name.empty()) continue;

			const auto result = search_one(name, options, binaries, manuals, sources);
			if (options.unusual && result.missing == 0) continue;

			ConsoleOutput::stdout_text(make_output_line(name, result, options.quiet));
		}

		return 0;
	}

} // namespace whereis

int wmain(int argc, wchar_t* argv[]) {
	std::vector<std::wstring_view> arguments;
	const auto argument_count = static_cast<std::size_t>(std::max(argc - 1, 0));
	const std::span argument_values{ argv + 1, argument_count };
	arguments.reserve(argument_values.size());
	for (const auto* argument : argument_values) arguments.emplace_back(argument);

	auto parsed = whereis::parse_arguments(arguments);
	if (!parsed) {
		whereis::ConsoleOutput::stderr_text(L"whereis: " + parsed.error() + L"\n\n");
		whereis::print_usage(true);
		return whereis::usage_error;
	}

	auto options = std::move(parsed.value());
	whereis::configure_directories(options);
	return whereis::run(options);
}
