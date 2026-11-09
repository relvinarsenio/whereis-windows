# whereis for Windows

`whereis` locates executable files, manual pages and documentation, and source directories on Windows.

It is derived from the FreeBSD `whereis(1)` utility and keeps its command-line interface and output format.
The search implementation is adapted for Windows.

## Contents

- [Overview](#overview)
- [Installation](#installation)
- [Usage](#usage)
- [Search behavior](#search-behavior)
- [Output and exit status](#output-and-exit-status)
- [Differences and limitations](#differences-and-limitations)
- [Building and testing](#building-and-testing)
- [Credits and license](#credits-and-license)

## Overview

`whereis` searches three categories:

- Binaries: executable files, scripts, and Windows application execution aliases.
- Manuals: man pages and standalone documentation files.
- Sources: directories named after the requested program.

Run `whereis` with a program name to search the available categories:

```powershell
whereis.exe pwsh
```

Use `-b` to search for binaries only:

```powershell
whereis.exe -b python winget wt
```

Display the usage information:

```powershell
whereis.exe --help
```

When no category option is specified, `whereis` searches each category with an available search root.

## Installation

Download `whereis.exe` from the [latest release](https://github.com/relvinarsenio/whereis-windows/releases/latest).

The executable requires no installer, administrator privileges, or Visual C++ runtime.

For a per-user installation, copy the executable to a directory such as:

```text
%LOCALAPPDATA%\Programs\whereis\bin
```

Add that directory to the user `PATH`:

```powershell
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\whereis\bin'
$UserPath   = [Environment]::GetEnvironmentVariable('Path', 'User')
$Entries    = @($UserPath -split ';' | Where-Object { $_.Trim() })

if ($Entries -notcontains $InstallDir) {
    [Environment]::SetEnvironmentVariable('Path', (($Entries + $InstallDir) -join ';'), 'User')
}

# Update PATH in the current session.
$env:Path = "$InstallDir;$env:Path"
```

Open a new terminal to use the updated `PATH`.

Verify the installation:

```powershell
where.exe whereis
whereis.exe -b whereis
```

To uninstall, delete the installation directory and remove its entry from the user `PATH`.

## Usage

### Synopsis

```text
whereis [-abmqsuxh] [-B dir ...] [-M dir ...] [-S dir ... -f] name ...
whereis --help
```

Single-letter options can be combined.
For example, `-ab` is equivalent to `-a -b`.

The directory options `-B`, `-M`, and `-S` must appear at the end of an option cluster.
For example, use `-aB` rather than `-Ba`.

A directory list ends when another directory option appears, or when `-f` or `--` appears.
Use `-f` to end the last directory list and begin the program names.

Option processing stops at the first argument that is not an option.
Put options before program names (for example, `whereis -b clang`).
Use `--` for names that begin with a hyphen.

### Options

| Option | Description |
|---|---|
| `-a` | Find all matches instead of the first match in each category. |
| `-b` | Search for executables. |
| `-m` | Search for manuals and documentation. |
| `-s` | Search for source directories. |
| `-B directory ...` | Override executable search directories. |
| `-M directory ...` | Override manual and documentation search directories. |
| `-S directory ...` | Override source search directories. |
| `-f` | Mark the end of directory lists. |
| `-q` | Do not print the program name label (`name:`). |
| `-u` | Show only programs with at least one missing result. |
| `-x` | Skip recursive source discovery. Direct child directory checks still run. |
| `-h`, `--help` | Show this help. |
| `--` | Treat all following arguments as program names. |

The `-B`, `-M`, and `-S` options replace the default search roots for their respective categories.

Each directory argument can contain multiple paths separated by semicolons (`;`).

Quote paths that contain spaces.
In PowerShell, use single or double quotes.
In `cmd.exe`, use double quotes.
Avoid a trailing backslash before the closing quotation mark because the sequence `\"` is interpreted as a literal quotation mark.

For example, use `"C:\Program Files\LLVM\bin"` rather than `"C:\Program Files\LLVM\bin\"`.

### Examples

Search all available categories:

```powershell
whereis.exe clang
```

Search for binaries:

```powershell
whereis.exe -b clang git cmake
```

Print all binary matches without the name label:

```powershell
whereis.exe -q -a -b pwsh
```

Search custom binary directories:

```powershell
whereis.exe -B 'C:\Program Files\LLVM\bin;D:\Tools\bin' -f clang
```

Search the manual pages of an MSYS2 installation:

```powershell
whereis.exe -m -M 'C:\msys64\usr\share\man' -f bash
```

Search source directories without recursion:

```powershell
whereis.exe -s -x -S 'D:\src;C:\Users\me\source\repos' -f openssl
```

Search multiple categories with custom roots:

```powershell
whereis.exe -B 'C:\Tools;D:\Apps\bin' -M 'C:\msys64\usr\share\man' -S 'D:\src' -f git clang
```

Print names that have no binary match:

```powershell
whereis.exe -u -b git cmake nonexistent-program
```

Search for a name that begins with a hyphen:

```powershell
whereis.exe -b -- -example
```

## Search behavior

### Category selection

When no category option is specified, `whereis` enables all three categories that have usable search roots.

Windows has no standard locations for manual pages or source trees.
`whereis` skips a category when it has no default search root.
An explicitly selected category requires at least one search directory.

### Name handling

If a program name contains a path, `whereis` uses its final component.

Before searching, `whereis` removes one matching compression suffix from the name:

```text
.bz2  .zst  .lzma  .gz  .xz  .Z  .z
```

### Binary search

`whereis` searches for binaries in this order:

1. Each directory in `PATH`, in order.
2. The Windows system directory, normally `C:\Windows\System32`.
3. The Windows directory.
4. Registry App Paths entries.

Within each directory, `whereis` tries the extensions listed in `PATHEXT`.
When `PATHEXT` is unset, `whereis` uses a built-in list of common executable and script extensions.
Names that already have a recognized executable extension are tried as given.

App Paths provides a fallback after directory searches.
For each candidate name, `whereis` checks `HKEY_CURRENT_USER` before `HKEY_LOCAL_MACHINE`.
It reads both the 64-bit and 32-bit Registry views.

The default value of this Registry entry supplies the executable path:

```text
Software\Microsoft\Windows\CurrentVersion\App Paths\<name>.exe
```

`whereis` uses the entry when it points to an existing file.

Windows application execution aliases, such as those in `%LOCALAPPDATA%\Microsoft\WindowsApps`, are included in binary searches.

The current directory is not a default binary search root.
Use `-B .` to include it as a custom root.

`whereis` preserves the file-name case stored in the filesystem.

### Manual pages and documentation

When `MANPATH` is set, `whereis` uses its directories as manual roots.

Otherwise, it derives roots from common Windows development environments.

For each `PATH` entry whose directory name is `bin`, it checks these locations relative to that entry:

```text
..\share\man
..\usr\share\man
..\..\usr\share\man
```

It also checks:

- `usr\share\man` below `MSYS2_ROOT`, `CYGWIN_ROOT`, and `GIT_INSTALL_ROOT`.
- `Git\usr\share\man` below `ProgramFiles` and `ProgramFiles(x86)`.

Within each root, `whereis` checks these layouts:

```text
root\
root\man\
root\doc\
root\docs\
root\share\man\
```

For a page named `name.section`, `whereis` checks both `manN\name.section` and the layout directory itself.
The supported sections are `1` through `9`, `1p`, and `3p`.

A manual page can have no suffix or one of these suffixes:

```text
.gz  .bz2  .xz  .zst  .html  .htm
```

`whereis` also searches for standalone documentation files in each layout directory.
Supported extensions are `.md`, `.markdown`, `.html`, `.htm`, `.pdf`, `.txt`, `.rst`, and `.adoc`.

`whereis` searches the filesystem.
It does not run `man` or apply its formatting and section-selection rules.

### Source directories

`whereis` checks each source root for a direct child directory with the requested name.

Unless `-x` is specified, it searches recursively below each root.
It skips the contents of these directories:

```text
.git  .hg  .svn  .vs  .cache
node_modules  packages  build  out  obj
```

The default source roots come from `SRCPATH` and `PORTSDIR`, along with these locations:

- `%VCPKG_ROOT%\ports`, when the directory exists.
- `source`, `source\repos`, and `src` below `%USERPROFILE%`.
- `Documents\source` and `Documents\GitHub` below `%USERPROFILE%`.
- `Documents\Visual Studio 2022\Projects` and `Documents\Visual Studio 2026\Projects` below `%USERPROFILE%`.
- `src` and `source` below `%SystemDrive%`, when those directories exist.

The `-S` option replaces these default roots.

### Environment variables

Directory lists use semicolons (`;`) as separators.
`whereis` expands `%NAME%` references in directory arguments and environment variable values.
A leading `~` represents `%USERPROFILE%`.

| Variable | Used for | Description |
|---|---|---|
| `PATH` | Binaries and manuals | Binary search directories. `whereis` also uses `bin` entries to locate default manual roots. |
| `PATHEXT` | Binaries | Extensions that `whereis` appends when searching for executable files. |
| `MANPATH` | Manuals | Manual page and documentation roots. |
| `SRCPATH` | Sources | Source search roots. |
| `PORTSDIR` | Sources | Additional source roots. |
| `VCPKG_ROOT` | Sources | Adds its `ports` directory as a source root. |
| `USERPROFILE` | Sources | Base directory for default per-user source locations. |
| `SystemDrive` | Sources | Base directory for default `src` and `source` locations. |
| `MSYS2_ROOT`, `CYGWIN_ROOT`, `GIT_INSTALL_ROOT` | Manuals | Used to locate `usr\share\man`. |
| `ProgramFiles`, `ProgramFiles(x86)` | Manuals | Used to locate Git manual pages. |

## Output and exit status

### Output

`whereis` prints one line for each program name:

```text
name: binary-path manual-path source-path
```

The line contains the paths that `whereis` finds.
When no path matches, it prints the name and colon:

```text
name:
```

The `-q` option omits the name label.
`whereis` encloses paths that contain spaces or tabs in double quotes.

The output format depends on the destination of standard output:

- Console output uses Unicode text and CR LF line endings.
- Redirected output uses UTF-8 and LF line endings.

When a file name contains malformed UTF-16, `whereis` writes replacement characters.

### Exit status

| Status | Meaning |
|---:|---|
| `0` | The command completed. An unmatched name does not produce an error. |
| `64` | Usage error, such as an unknown option or missing program name. |
| `65` | No usable category or search directory. |

Statuses `64` and `65` correspond to `EX_USAGE` and `EX_DATAERR` in `sysexits.h`.

## Differences and limitations

### Differences from FreeBSD

The Windows implementation follows the FreeBSD command-line interface and output format.
The search behavior differs where Windows uses different filesystem and command-discovery conventions.

| Area | FreeBSD | Windows |
|---|---|---|
| Binary search roots | `user.cs_path`, `/usr/libexec`, then `PATH` | `PATH`, system directory, Windows directory, then App Paths |
| Executable check | Regular file with an execute bit | Existing file with an extension from `PATHEXT` |
| Manual search | Runs `man -w` | Searches the filesystem |
| Source search | Fixed `/usr/src` locations and `/usr/ports` subdirectories, with a `locate` fallback | Configured roots with recursive directory search |
| Case sensitivity | Case-sensitive | Case-insensitive |
| Name stripping | Leading path, `s.` prefix, `,v` suffix, `.z`, `.Z`, `.gz`, and `.bz2` | Leading path and `.bz2`, `.zst`, `.lzma`, `.gz`, `.xz`, `.Z`, and `.z` |
| Directory list separator | `:` | `;` |
| Paths with spaces | Printed as is | Enclosed in double quotes |
| Help option | None | `-h`, `--help` |

### Limitations

`whereis` searches files and directories.
It does not resolve commands through a shell.

- PowerShell aliases, functions, and cmdlets are outside its search scope. Use `Get-Command` to locate them.
- `whereis` does not read AppX package manifests. It finds an application execution alias when its file exists in a searched directory.
- App Paths entries can point to locations that change after an application update.
- The current directory is not searched unless it appears in a search root.
- `whereis` finds `.ps1` scripts only when `.PS1` appears in `PATHEXT`.
- Manual and source results depend on the configured or detected roots. An installed program may have no matching manual page or source directory.
- A recursive source search can find an unrelated directory with the requested name. Use `-b` to search only for binaries or `-x` to skip recursion.
- In excluded directories, `whereis` checks the directory name but does not search its contents.

### PowerShell output

`whereis` writes redirected output as UTF-8.
PowerShell may decode native program output using a different encoding, which can corrupt non-ASCII paths.

Check the encoding PowerShell uses to read native output:

```powershell
[Console]::OutputEncoding.CodePage
```

If the code page does not match UTF-8, set the console output encoding before running `whereis`:

```powershell
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
```

The `$OutputEncoding` variable controls the encoding PowerShell uses when sending data to native programs.
It does not control how PowerShell reads their output.

PowerShell redirection also depends on the version:

| PowerShell version | Redirected native output |
|---|---|
| Windows PowerShell 5.1 | UTF-16LE |
| PowerShell 7.0 through 7.3 | UTF-8 without a byte order mark |
| PowerShell 7.4 and later | Preserves the original byte stream |

PowerShell 7.4 and later preserve the byte stream when redirecting native standard output directly to a file.
This behavior does not apply when stderr is merged into stdout with `2>&1`, because PowerShell treats the combined streams as text.

To preserve the UTF-8 bytes when using older PowerShell versions, redirect through `cmd.exe`:

```powershell
cmd /c "whereis.exe -b pwsh > out.txt"
```

## Building and testing

### Requirements

- Windows 10 or Windows 11.
- CMake 3.25 or later.
- A C++23 compiler and the Windows SDK.
- A build tool supported by CMake, such as Visual Studio or Ninja.

### Build

Build the Release configuration with the provided CMake workflow preset:

```powershell
cmake --workflow --preset release
```

Alternatively, run the configure, build, and test steps individually:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

A `debug` preset is also available.
It builds in `out\build\debug`.

The presets target x64 and use CMake's default generator.
To use a different generator, set the CMAKE_GENERATOR environment variable.

Set `CMAKE_GENERATOR` to use Ninja:

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
cmake --preset release
```

Run the build from Developer PowerShell or another environment with the compiler and Windows SDK available.

With a multi-configuration generator such as Visual Studio, the executable is located at:

```text
out\build\release\Release\whereis.exe
```

With a single-configuration generator such as Ninja, the executable is located at:

```text
out\build\release\whereis.exe
```

When building without presets and using a multi-configuration generator, pass `--config Release` to `cmake --build` and `-C Release` to `ctest`.
Otherwise, CMake uses the default configuration, which is commonly Debug.

Install the executable with CMake:

```powershell
cmake --install out\build\release --config Release --prefix "$env:LOCALAPPDATA\Programs\whereis"
```

CMake installs the executable as `bin\whereis.exe` below the selected prefix.

### Build options

| Option | Default | Description |
|---|---:|---|
| `BUILD_TESTING` | `ON` | Build the unit tests, Unicode integration test, and CTest acceptance suite. |
| `WHEREIS_BUILD_PROFILE` | `Release` | Default build type for single-configuration generators when `CMAKE_BUILD_TYPE` is not set. |
| `WHEREIS_ENABLE_IPO` | `ON` | Enable link-time optimization for Release and RelWithDebInfo builds when the toolchain supports it. |
| `WHEREIS_ENABLE_HARDENING` | `ON` | Enable supported hardening options. MSVC uses SDL and Control Flow Guard. GCC and Clang use stack protection in Release builds. |
| `WHEREIS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

### Testing

CTest runs the unit tests, a Unicode integration test, and a CMake-script acceptance suite. The acceptance suite does not require PowerShell.

When tests are enabled, CMake obtains GoogleTest from GitHub through FetchContent. Configuration may need network access when GoogleTest is not already available locally.

Run the tests with the Release preset:

```powershell
ctest --preset release
```

Or specify the build directory and configuration:

```powershell
ctest --test-dir out\build\release -C Release --output-on-failure
```

Unit tests cover option parsing, path and environment handling, output formatting, and search routines.

The Unicode test checks that filenames containing unpaired UTF-16 surrogates are written as U+FFFD in redirected output. It is skipped if the filesystem rejects such filenames.

The acceptance suite creates an isolated temporary directory tree and checks:

- Help output, a missing program name, and unknown options.
- Names containing dots, paths containing spaces, and valid Unicode names.
- UTF-8 redirected output and LF line endings.
- `-a`, `-q`, `-u`, `-x`, option clusters, `-f`, and `--`.
- `PATHEXT` handling and custom binary directories.
- Sectioned manual pages and standalone documentation.
- Direct and recursive source searches.
- Agreement between the first result and `where.exe` for common commands, including `pwsh`, `python`, `winget`, `wt`, `find`, and `sort`.

The `where.exe` comparison is skipped when the Windows locator is unavailable or cannot find a command.

## Credits and license

### See also

- `where.exe /?` for the Windows command-line file locator.
- `Get-Command` for PowerShell command discovery.
- [`whereis(1)`](https://man.freebsd.org/cgi/man.cgi?query=whereis&sektion=1) for the FreeBSD manual page.

### History

Jörg Wunsch wrote the original FreeBSD `whereis` in 2002 as a rewrite that preserves the interface of the 4.3BSD utility.

Alfie Ardinata wrote the Windows port.
It retains the original command-line interface and output format while adapting the search to Windows.

### License

This program is distributed under the BSD 2-Clause license, the license of the original FreeBSD source.
See [`LICENSE`](LICENSE) for the full text.

The original FreeBSD source is Copyright © 2002, Jörg Wunsch.

The Windows port is Copyright © 2026, Alfie Ardinata.

Retain all copyright notices, conditions, and disclaimers when redistributing the source or binary.
