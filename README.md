# whereis for Windows

A Windows-native implementation of the BSD `whereis(1)` command-line interface, written in C++23.

`whereis` locates three kinds of items associated with a program:

- **Binary files** — executable files and Windows application execution aliases.
- **Manual pages and documentation** — man-page layouts and common standalone documentation files.
- **Source directories** — directories named after the requested program, including repositories found beneath configured source roots.

The command keeps the familiar BSD options and output format. It uses Windows paths, environment variables, the Registry, and Win32 APIs instead of trying to emulate Unix system calls.

## Contents

- [Quick start](#quick-start)
- [Synopsis](#synopsis)
- [Description](#description)
- [Options](#options)
- [How files are found](#how-files-are-found)
- [Environment](#environment)
- [Output](#output)
- [Exit status](#exit-status)
- [Examples](#examples)
- [Installation](#installation)
- [Building](#building)
- [Testing](#testing)
- [Limitations](#limitations)
- [Implementation notes](#implementation-notes)
- [License](#license)

## Quick start

Run `whereis.exe` followed by one or more program names:

```powershell
whereis.exe pwsh
whereis.exe python winget wt
whereis.exe -a find sort
```

When no search category is specified, `whereis` attempts binary, manual, and source discovery. On Windows, manual and source roots are not standardized. A category without a plausible default search root is skipped during the default search.

To search only for executables, use `-b`:

```powershell
whereis.exe -b pwsh python git
```

To show every match rather than the first match in each category, use `-a`:

```powershell
whereis.exe -a -b pwsh
```

For a list of options, run:

```powershell
whereis.exe -h
```

## Synopsis

```text
whereis [-abmqsuxh] [-B directory ...] [-M directory ...] [-S directory ... -f] name ...
whereis --help
```

Options may be combined into clusters. For example, `-ab` is equivalent to `-a -b`. The directory options `-B`, `-M`, and `-S` may appear at the end of a cluster, as in `-aB`.

Directory lists continue until another directory option, `-f`, or `--` is encountered. Use `-f` to mark the end of directory lists and the beginning of program names.

## Description

For each requested name, `whereis` searches the enabled categories and prints the locations it finds. The categories are searched in this order:

1. Binary files.
2. Manual pages and documentation.
3. Source directories.

By default, the first match in each category is printed. The `-a` option requests all matches in each enabled category.

If a query includes a path, only its final component is used as the program name. Common compressed-manual suffixes such as `.gz`, `.bz2`, `.xz`, `.zst`, `.Z`, and `.z` are removed before searching.

### Default categories

If none of `-b`, `-m`, or `-s` is specified, all three categories are initially enabled. Windows does not provide universal locations for man pages or source trees. Therefore, categories with no default search roots are disabled for that invocation. If no usable category remains, `whereis` reports an error.

When a category is selected explicitly, configure its search roots with the corresponding environment variable or directory option. An explicitly selected category with no search directories is an error.

## Options

The following options are supported:

| Option | Description |
|---|---|
| `-a` | Display all matches in each enabled category. Without this option, only the first match in each category is displayed. |
| `-b` | Search for binary and executable files. |
| `-m` | Search for manual pages and documentation. |
| `-s` | Search for source directories. |
| `-B directory ...` | Use the specified directories for binary searches instead of the default binary search roots. |
| `-M directory ...` | Use the specified directories for manual and documentation searches instead of the defaults. |
| `-S directory ...` | Use the specified directories for source searches instead of the defaults. |
| `-f` | End a directory list and treat the remaining arguments as program names. |
| `-q` | Do not print the program-name label. Only matching paths are printed. |
| `-u` | Print only names for which at least one enabled category has no match. |
| `-x` | Skip recursive source discovery. Direct `source-root\name` checks are still performed. |
| `-h`, `--help` | Display the usage summary and exit. |
| `--` | Stop option processing. Every following argument is treated as a program name. |

`-B`, `-M`, and `-S` accept one or more directory arguments. A directory argument may itself contain a semicolon-separated list. Quote a path containing spaces so that the shell passes it as one argument.

The `-f` option is useful when several directory lists are specified:

```powershell
whereis.exe -B 'C:\Tools;D:\Apps\bin' -M 'C:\msys64\usr\share\man' -S 'D:\src' -f git clang
```

## How files are found

### Binary files (`-b`)

Binary searches use the following order:

1. Directories from `PATH`, in their existing order.
2. The Windows system directory, normally `C:\Windows\System32`.
3. The Windows directory.
4. Registry **App Paths** entries, used only as a fallback after directory searches.

For each directory, the requested filename is checked against the executable extensions in `PATHEXT`. If `PATHEXT` is not available, a built-in list of common Windows executable and script extensions is used. If the query already has a recognized executable extension, that filename is checked as given.

App Execution Aliases in `%LOCALAPPDATA%\Microsoft\WindowsApps` are checked using Win32 file attributes. This allows filesystem entries implemented as AppExecLink reparse points to be discovered even when the C++ standard library does not report them as regular files.

The current working directory is not added implicitly to the binary search path. Add it explicitly with `-B` when it should be searched.

### Manual pages and documentation (`-m`)

If `MANPATH` is set, its directories are used. Otherwise, `whereis` looks for likely manual-page roots associated with `PATH` entries ending in `bin`, common MSYS2/Cygwin/Git installations, and Git installations under Program Files.

Within each root, the search checks common layouts including:

```text
root\
root\man\
root\doc\
root\docs\
root\share\man\
```

It checks section directories `man1` through `man9`, as well as `man1p` and `man3p`, and also checks for pages placed directly in a layout directory. Common compressed suffixes are recognized. Common standalone documentation formats such as Markdown, HTML, PDF, plain text, reStructuredText, and AsciiDoc are also searched.

This is a filesystem search. It does not invoke a Unix `man` command or reproduce the formatting and section-selection rules of a complete `man` implementation.

### Source directories (`-s`)

The source search checks each configured root for a direct child whose name matches the query. Unless `-x` is specified, it then searches recursively beneath those roots, up to eight directory levels.

Common generated or dependency directories are pruned during recursive traversal, including:

```text
.git  .hg  .svn  .vs  .cache
node_modules  packages  build  out  obj
```

The depth limit and pruning rules keep an ordinary source search bounded. They also mean that a deeply nested source directory, or one beneath a pruned directory, may not be reported.

## Environment

Directory lists use the Windows semicolon (`;`) separator. Environment-variable references written as `%NAME%` in a directory path are expanded.

| Variable | Used for | Behavior |
|---|---|---|
| `PATH` | Binary search | Searched before the Windows system and Windows directories. |
| `PATHEXT` | Binary search | Supplies the executable extensions to try when the query has no recognized executable extension. |
| `MANPATH` | Manual search | Supplies manual and documentation roots. |
| `SRCPATH` | Source search | Supplies source roots. |
| `PORTSDIR` | Source search | Adds a ports/source root. |
| `VCPKG_ROOT` | Source search | Adds the `ports` directory beneath this root when it exists. |
| `USERPROFILE` | Default source search | Used to check common per-user source locations. |
| `SystemDrive` | Default source search | Used to check `src` and `source` directories at the drive root. |
| `MSYS2_ROOT` | Default manual search | Used to check `usr\share\man`. |
| `CYGWIN_ROOT` | Default manual search | Used to check `usr\share\man`. |
| `GIT_INSTALL_ROOT` | Default manual search | Used to check `usr\share\man`. |

The `-B`, `-M`, and `-S` options replace the default roots for their respective categories. They do not append to those defaults.

## Output

By default, each result is printed in the BSD-style form:

```text
name: binary-path manual-path source-path
```

Only paths that were found are included. If no path is found and `-q` is not used, the name and colon are still printed:

```text
name:
```

Paths containing spaces or tabs are surrounded by double quotes. With `-q`, the name and colon are omitted:

```powershell
whereis.exe -q -b pwsh
```

When standard output is a Windows console, text is written as Unicode through the console API and uses Windows console line endings. When output is redirected to a file or pipe, text is encoded as UTF-8 and lines end with LF (`\n`).

## Exit status

`whereis` returns one of the following status codes:

| Status | Meaning |
|---:|---|
| `0` | The command completed. This includes searches for which no matching item was found. |
| `64` | Invalid command-line usage, such as an unknown option or a missing program name. |
| `65` | No usable search category or search directory is available for the requested operation. |

A missing program is not itself an error. This follows the behavior of the BSD utility.

## Examples

Search all available default categories for a program:

```powershell
whereis.exe clang
```

Search only the binary category:

```powershell
whereis.exe -b clang git cmake
```

Display every binary match found through `PATH` and the fallback locations:

```powershell
whereis.exe -a -b pwsh
```

Search a custom executable directory. The directory list must be terminated with `-f`:

```powershell
whereis.exe -B 'C:\Program Files\LLVM\bin;D:\Tools\bin' -f clang
```

Search manual pages installed by MSYS2:

```powershell
whereis.exe -m -M 'C:\msys64\usr\share\man' -f bash
```

Search source repositories without recursively scanning the configured roots:

```powershell
whereis.exe -s -x -S 'D:\src;C:\Users\me\source\repos' -f openssl
```

Print only names for which the binary category has no match:

```powershell
whereis.exe -u -b git cmake nonexistent-program
```

Treat a string beginning with a hyphen as a program name:

```powershell
whereis.exe -b -- -example
```

## Installation

A per-user installation does not require administrator privileges. One suggested location is:

```text
%LOCALAPPDATA%\Programs\whereis\bin\whereis.exe
```

From the repository root, after building the Release configuration:

```powershell
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\whereis\bin'

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item '.\build\Release\whereis.exe' (Join-Path $InstallDir 'whereis.exe') -Force
```

For a single-configuration generator such as Ninja, the executable is normally at `build\whereis.exe` instead. The `cmake --install` command in the build section avoids depending on this generator-specific layout.

Add the directory to the current user's `PATH`:

```powershell
$UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$Entries = @($UserPath -split ';' | Where-Object { $_.Trim() })

if ($Entries -notcontains $InstallDir) {
    [Environment]::SetEnvironmentVariable(
        'Path', (($Entries + $InstallDir) -join ';'), 'User'
    )
}

# Update PATH in this PowerShell session as well.
$env:Path = "$InstallDir;$env:Path"
```

New terminal sessions inherit the updated user `PATH`. To verify the installation:

```powershell
where.exe whereis
Get-Command whereis
whereis.exe -b pwsh
```

To uninstall, remove the installed `whereis` directory and remove its `bin` entry from the user's `PATH`.

## Building

### Requirements

- Windows 10 or Windows 11.
- CMake 3.25 or newer.
- A C++23-capable Windows toolchain and Windows SDK.
- A supported build tool, such as a Visual Studio generator, Ninja, or NMake.

The project does not pin a Visual Studio release, generator, architecture, or toolset. CMake uses its normal generator-discovery rules, so the same presets can be used with the Visual Studio version installed today, a later release, or another supported generator.

### Configure and build with presets

List the available presets:

```powershell
cmake --list-presets
```

Configure and build Release:

```powershell
cmake --preset release
cmake --build --preset release
```

Run the Release tests:

```powershell
ctest --preset release
```

For a separate Debug build:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The Release preset uses the conventional repository-local `build` directory. The Debug preset uses `build-debug`, keeping the two configurations separate for single-configuration generators. No custom runtime output directory is set: CMake and the selected generator retain their normal output layout.

For a multi-configuration generator such as Visual Studio, the Release executable is normally placed at `build\Release\whereis.exe`, while Debug is placed under the corresponding configuration directory. For a single-configuration generator such as Ninja, the selected configuration is applied during configuration and the executable is normally placed directly in `build\whereis.exe`.

**Important:** with Visual Studio's multi-configuration generator, running `cmake --build build` without `--config` generally builds the generator's default configuration (commonly Debug). Use `cmake --build --preset release` or `cmake --build build --config Release` when you intend to run Release tests. Likewise, use `ctest --preset release` or specify `-C Release` so CTest selects the matching executable.

### Generator selection

The presets deliberately omit `generator`, `architecture`, and `toolset`. CMake selects its default generator using its normal discovery rules. To choose a generator explicitly, set `CMAKE_GENERATOR` before configuring. For example, with Ninja:

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The selected compiler must already be available in the environment. For Visual Studio command-line builds, use a Developer PowerShell/Command Prompt or another environment where the Visual C++ toolchain and Windows SDK are available. To select a particular Visual Studio generator, check the generator names supported by the installed CMake with `cmake --help` and set the exact name through `CMAKE_GENERATOR`.

Support for a newly released Visual Studio generator depends on the installed CMake version. The project presets do not need to change when CMake adds support for a newer Visual Studio release.

### Configure manually

Presets are conveniences, not a requirement. For a Ninja Release build:

```powershell
cmake -S . -B build-ninja -G Ninja `
    -DWHEREIS_BUILD_PROFILE=Release `
    -DBUILD_TESTING=ON `
    -DWHEREIS_ENABLE_IPO=ON `
    -DWHEREIS_ENABLE_HARDENING=ON

cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

For a multi-configuration generator, select the configuration at build and test time:

```powershell
cmake -S . -B build-vs -G '<generator name from cmake --help>' `
    -DBUILD_TESTING=ON `
    -DWHEREIS_ENABLE_IPO=ON `
    -DWHEREIS_ENABLE_HARDENING=ON

cmake --build build-vs --config Release
ctest --test-dir build-vs -C Release --output-on-failure
```

### Build options

The build uses target-scoped CMake properties and options. It does not modify global compiler flags.

| Option | Default | Description |
|---|---:|---|
| `BUILD_TESTING` | `ON` | Register the CTest acceptance suite. |
| `WHEREIS_BUILD_PROFILE` | `Release` | Default configuration for single-configuration generators when `CMAKE_BUILD_TYPE` is not explicitly set. Multi-configuration generators select the configuration at build/test time. |
| `WHEREIS_ENABLE_IPO` | `ON` | Enable interprocedural optimization (LTO) for Release and RelWithDebInfo when supported by the active compiler and linker. |
| `WHEREIS_ENABLE_HARDENING` | `ON` | Enable supported compiler and linker hardening options. |
| `WHEREIS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

CMake's Release configuration supplies the normal optimization flags for the selected compiler. For MSVC this normally includes `/O2`; GCC- and Clang-based toolchains receive their standard Release optimization flags. When IPO is available, CMake enables link-time optimization as well. MSVC Release links also enable reference elimination and identical COMDAT folding. Supported hardening options include SDL and Control Flow Guard for MSVC, and stack protection for GCC/Clang toolchains.

IPO is optional. This utility is a single translation unit, so link-time optimization may provide only a modest benefit. CMake checks whether the active compiler and linker support IPO; unsupported toolchains continue with their normal Release optimization.

For additional diagnostics, configure a separate build directory with warnings-as-errors:

```powershell
cmake -S . -B build-strict -G Ninja `
    -DWHEREIS_BUILD_PROFILE=Debug `
    -DBUILD_TESTING=ON `
    -DWHEREIS_WARNINGS_AS_ERRORS=ON

cmake --build build-strict
ctest --test-dir build-strict --output-on-failure
```

### Install

Install the Release executable and documentation using CMake's install rules:

```powershell
cmake --install build --config Release `
    --prefix "$env:LOCALAPPDATA/Programs/whereis"
```

The executable is installed as:

```text
%LOCALAPPDATA%\Programs\whereis\bin\whereis.exe
```

Add that `bin` directory to the user's `PATH` to invoke `whereis` from any terminal.

## Testing

Tests are registered with CTest and implemented as a CMake script. PowerShell is not required by the test suite.

After building Release, run:

```powershell
ctest --preset release
```

Or run the tests directly against the conventional build directory:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

For verbose test output:

```powershell
ctest --test-dir build -C Release -V
```

The acceptance suite creates an isolated temporary test tree and checks:

- Help output, missing arguments, and unknown options.
- Executable names containing dots, paths containing spaces, and Unicode queries/filenames.
- `-a`, `-q`, clustered options, `-f`, and `--`.
- `PATHEXT` handling and custom binary directories.
- BSD-style `-u` behavior.
- Sectioned manual pages and standalone documentation.
- Direct and recursive source discovery, including `-x`.
- LF line endings for redirected output.
- First-result agreement with `where.exe` for common commands such as `pwsh`, `python`, `winget`, `wt`, `find`, and `sort`, when those commands are available on the host.

The `where.exe` comparisons are conditional because Windows installations do not all have the same optional applications or App Execution Aliases. A command that is not present is reported as skipped; deterministic filesystem and option tests remain mandatory.

## Limitations

`whereis` is a filesystem-oriented locator. It is not a complete replacement for Windows command resolution.

- It does not resolve PowerShell aliases, functions, cmdlets, or profile-defined commands. Use `Get-Command` for PowerShell command discovery.
- It does not inspect AppX package manifests. App Execution Aliases are found through their filesystem entries when those entries are present in a searched directory.
- Registry App Paths entries are a fallback after directory searches. They may refer to package-specific locations that change when an application is updated.
- The current working directory is not searched implicitly for binaries.
- PowerShell scripts such as `.ps1` are searched only when their extension is included in `PATHEXT`.
- Manual and source discovery depend on configured or detected roots. A result is not guaranteed merely because a program is installed.
- Recursive source discovery is limited to eight levels and skips the directories listed above.

For command resolution in PowerShell, use:

```powershell
Get-Command pwsh
```

For executable lookup using Windows' built-in search utility, use:

```powershell
where.exe pwsh
```

## Implementation notes

The implementation is intentionally kept in one C++ translation unit. Its three search components (`BinarySearcher`, `ManualSearcher`, and `SourceSearcher`) use a shared `SearchRequest` and satisfy the `SearchBackend` concept. They are composed without a common base class, `virtual` functions, or runtime polymorphism.

Owned resources use RAII. In particular, `RegistryKey` owns an `HKEY` and closes it automatically. Standard-library strings, paths, and containers manage their own storage. Handles returned by `GetStdHandle` are borrowed and are not closed by the program.

Collection processing uses range-based loops, ranges algorithms, and iterator-based traversal. The implementation avoids numeric-index loops so that code operates on elements or iterator state directly.

The design takes inspiration from the clarity and restraint of the FreeBSD utility: preserve a small command-line contract, isolate operating-system details, and avoid abstractions that do not solve a real problem. The Windows implementation is not intended to reproduce FreeBSD's internal use of `sysctl`, `man`, or `locate`.

## License

The source retains the original BSD 2-Clause license notice and Jörg Wunsch's 2002 copyright attribution, alongside the copyright attribution for the Windows port's contributions by Alfie Ardinata. The complete license text is provided in [`LICENSE`](LICENSE) and is installed alongside the README. Preserve all applicable copyright notices, license conditions, and disclaimers when redistributing the source or binary.
