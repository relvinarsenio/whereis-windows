# whereis for Windows

`whereis` is a Windows implementation of the FreeBSD `whereis(1)` utility.
It locates executables, manual pages and documentation, and source directories.

It keeps the familiar command-line interface while adapting the search behavior to Windows paths, environment variables, and Registry locations.

## Contents

- [Overview](#overview)
- [Installation](#installation)
- [Usage](#usage)
- [Search Behavior](#search-behavior)
- [Output and Exit Status](#output-and-exit-status)
- [Compatibility and Limitations](#compatibility-and-limitations)
- [Building and Testing](#building-and-testing)
- [Credits and License](#credits-and-license)

## Overview

`whereis` searches three categories:

- **Binaries** — executables, scripts, and Windows application execution aliases.
- **Manuals** — man pages and standalone documentation files.
- **Sources** — directories named after the requested program.

Try it:

```powershell
whereis.exe pwsh
whereis.exe -b python winget wt
whereis.exe --help
```

Without a category option, `whereis` searches every category with an available search location.

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
Verify the installation with:

```powershell
where.exe whereis
whereis.exe -b whereis
```

To uninstall, delete the installation directory and remove it from the user `PATH`.

## Usage

### Synopsis

```text
whereis [-abmqsuxh] [-B directory ...] [-M directory ...] [-S directory ... -f] name ...
whereis --help
```

Single-letter options can be combined.
For example, `-ab` is equivalent to `-a -b`.

### Options

| Option | Description |
|---|---|
| `-a` | Print all matches instead of the first match in each category. |
| `-b` | Search for binaries. |
| `-m` | Search for manuals and documentation. |
| `-s` | Search for source directories. |
| `-B directory ...` | Use these directories as binary search roots. |
| `-M directory ...` | Use these directories as manual search roots. |
| `-S directory ...` | Use these directories as source search roots. |
| `-f` | End the directory lists. Remaining arguments are program names. |
| `-q` | Omit the `name:` label and print only paths. |
| `-u` | Print only names for which an enabled category has no match. |
| `-x` | Skip recursive source searches. Direct child checks are still performed. |
| `-h`, `--help` | Print usage information. |
| `--` | Stop option processing. Following arguments are treated as names. |

The directory options replace the default roots for their respective categories; they do not add to them.

Directory arguments may contain multiple paths separated by semicolons (`;`).
A directory list ends when another directory option is encountered, or when `-f` or `--` is used.

Quote paths containing spaces.
In `cmd.exe`, avoid ending a quoted path with a backslash.
For example, use `"C:\Program Files\LLVM\bin"` rather than `"C:\Program Files\LLVM\bin\"`.

### Examples

Search all available categories:

```powershell
whereis.exe clang
```

Search only for binaries:

```powershell
whereis.exe -b clang git cmake
```

Print every binary match without the name label:

```powershell
whereis.exe -q -a -b pwsh
```

Search custom binary directories:

```powershell
whereis.exe -B 'C:\Program Files\LLVM\bin;D:\Tools\bin' -f clang
```

Search manual pages from an MSYS2 installation:

```powershell
whereis.exe -m -M 'C:\msys64\usr\share\man' -f bash
```

Search source directories without recursion:

```powershell
whereis.exe -s -x -S 'D:\src;C:\Users\me\source\repos' -f openssl
```

Use different roots for multiple categories:

```powershell
whereis.exe -B 'C:\Tools;D:\Apps\bin' -M 'C:\msys64\usr\share\man' -S 'D:\src' -f git clang
```

Find names that have no binary match:

```powershell
whereis.exe -u -b git cmake nonexistent-program
```

Search for a name beginning with a hyphen:

```powershell
whereis.exe -b -- -example
```


## Search Behavior

### Category Selection

If no category option is specified, all three categories are enabled when they have usable search roots.

Windows does not provide standard locations for manual pages or source trees.
A category without a default search root is skipped.
Selecting such a category explicitly with `-b`, `-m`, or `-s` requires at least one search directory.

### Binary Search

Binary search follows this order:

1. Directories listed in `PATH`.
2. The Windows system directory, normally `C:\Windows\System32`.
3. The Windows directory.
4. Registry **App Paths** entries.

Within each directory, `whereis` tries the extensions listed in `PATHEXT`.
If `PATHEXT` is unavailable, a built-in list of common executable and script extensions is used.
Names that already have a recognized executable extension are tried as given.

App Paths is used as a fallback after directory searches.
`HKEY_CURRENT_USER` is checked before `HKEY_LOCAL_MACHINE`, using both the 64-bit and 32-bit Registry views.
The default value of the following entry is used when it points to an existing file:

```text
Software\Microsoft\Windows\CurrentVersion\App Paths\<name>.exe
```

Windows application execution aliases, such as those in `%LOCALAPPDATA%\Microsoft\WindowsApps`, are also considered when searching for executables.

The current directory is not searched automatically.
Use `-B .` to search it as a custom binary root.

### Manual Pages and Documentation

If `MANPATH` is set, its directories are used as manual roots.

Otherwise, roots are detected from common Windows development environments:

- Directories relative to `bin` entries in `PATH`, including `share\man` locations.
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

Man pages are searched by section, from `1` through `9`, including `1p` and `3p`.
Both `manN\name.section` and files placed directly in a layout directory are considered.

Supported man-page suffixes include `.gz`, `.bz2`, `.xz`, `.zst`, `.html`, and `.htm`.

Standalone documentation can use `.md`, `.markdown`, `.html`, `.htm`, `.pdf`, `.txt`, `.rst`, or `.adoc`.

This is a filesystem search.
`whereis` does not run `man` or format manual pages.

### Source Directories

Each source root is first checked for a direct child directory with the requested name.

Unless `-x` is specified, the search continues recursively.
Traversal is limited to eight directory levels and skips the contents of common generated or version-control directories, including:

```text
.git  .hg  .svn  .vs  .cache
node_modules  packages  build  out  obj
```

Default source roots come from:

- `SRCPATH` and `PORTSDIR`.
- `%VCPKG_ROOT%\ports`, when present.
- Common source locations below `%USERPROFILE%`, including `source`, `source\repos`, `src`, `Documents\source`, `Documents\GitHub`, and Visual Studio project directories.
- `src` and `source` below `%SystemDrive%`, when present.

### Environment Variables

Directory lists use semicolons (`;`) as separators.
`%NAME%` references are expanded, and a leading `~` represents `%USERPROFILE%`.

| Variable | Purpose |
|---|---|
| `PATH` | Binary search directories and the basis for detecting some manual roots. |
| `PATHEXT` | Executable and script extensions. |
| `MANPATH` | Manual and documentation roots. |
| `SRCPATH` | Source search roots. |
| `PORTSDIR` | Additional source roots. |
| `VCPKG_ROOT` | Adds its `ports` directory as a source root. |
| `USERPROFILE` | Base for default per-user source locations. |
| `SystemDrive` | Base for default `src` and `source` locations. |
| `MSYS2_ROOT`, `CYGWIN_ROOT`, `GIT_INSTALL_ROOT` | Used to locate manual pages. |
| `ProgramFiles`, `ProgramFiles(x86)` | Used to locate Git manual pages. |

## Output and Exit Status

### Output

Each program name produces one output line:

```text
name: binary-path manual-path source-path
```

Only paths that were found are printed.
If nothing is found, the name and colon are still printed unless `-q` is used:

```text
name:
```

Paths containing spaces or tabs are enclosed in double quotes.

Output encoding depends on where standard output is directed:

- **Console:** Unicode text with CR LF line endings.
- **File or pipe:** UTF-8 text with LF line endings.

Malformed UTF-16 file names are written using replacement characters.

### Exit Status

| Status | Meaning |
|---:|---|
| `0` | The command completed. A name that was not found is not an error. |
| `64` | Usage error, such as an unknown option or missing program name. |
| `65` | No usable category or search directory. |

Statuses `64` and `65` correspond to `EX_USAGE` and `EX_DATAERR` in `sysexits.h`.

## Compatibility and Limitations

### Differences from FreeBSD

The Windows implementation keeps the familiar interface and output format, but search behavior differs where Windows has no direct equivalent.

| Area | FreeBSD | Windows |
|---|---|---|
| Binary search roots | `user.cs_path`, `/usr/libexec`, then `PATH` | `PATH`, system directory, Windows directory, then App Paths |
| Executable check | Regular file with an execute bit | Existing file with an extension from `PATHEXT` |
| Manual search | Runs `man -w` | Searches the filesystem |
| Source search | Fixed source locations and `locate` fallback | Configured roots and bounded recursive search |
| Name matching | Case-sensitive | Case-insensitive |
| Name stripping | Leading path, `s.` prefix, `,v` suffix, and compression suffixes | Leading path and compression suffixes |
| Directory separator | `:` | `;` |
| Paths with spaces | Printed as is | Enclosed in double quotes |
| Help option | None | `-h`, `--help` |

The exit status for an unsuccessful search, including values `64` and `65`, follows the FreeBSD utility.

### Limitations

`whereis` locates files and directories.
It does not reproduce shell command resolution.

- PowerShell aliases, functions, and cmdlets are not resolved.
  Use `Get-Command` for those.
- AppX package manifests are not read.
  An application execution alias is found only when its file exists in a searched directory.
- App Paths entries may become outdated when applications move or are updated.
- The current directory is not searched unless specified explicitly.
- `.ps1` scripts are found only when `.PS1` is included in `PATHEXT`.
- Manual and source results depend on the available search roots.
  An installed program is not guaranteed to have a manual or source result.
- Recursive source searches may find unrelated directories with the same name.
  Use `-b` to search only for binaries or `-x` to skip recursion.
- Directories at the recursion limit and excluded directories are still checked by name, but their contents are not searched.

Redirected output is UTF-8.
Windows PowerShell 5.1 may decode native UTF-8 output using a legacy code page, causing non-ASCII paths to appear incorrectly.
Set the output encoding before running the command:

```powershell
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
```

PowerShell redirection also varies by version: Windows PowerShell 5.1 uses UTF-16LE, PowerShell 7.0–7.3 uses UTF-8 without a BOM, and PowerShell 7.4 and later preserve the native byte stream.

## Building and Testing

### Requirements

- Windows 10 or Windows 11.
- CMake 3.25 or later.
- A C++23 compiler and the Windows SDK.
- A build tool supported by CMake, such as Visual Studio or Ninja.

### Build

Build the Release configuration using the provided CMake preset:

```powershell
cmake --preset release
cmake --build --preset release
```

A `debug` preset is also available.

The presets use CMake's default generator.
To use Ninja, for example:

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
cmake --preset release
```

Run the build from a Developer PowerShell or another environment where the compiler and Windows SDK are available.

With a multi-configuration generator such as Visual Studio, the executable is produced under `build\Release`.
With a single-configuration generator such as Ninja, it is produced under `build`.

To install the executable using CMake:

```powershell
cmake --install build --config Release --prefix "$env:LOCALAPPDATA/Programs/whereis"
```

The executable is installed as `bin\whereis.exe` below the chosen prefix.

### Build Options

| Option | Default | Description |
|---|---:|---|
| `BUILD_TESTING` | `ON` | Enable the CTest acceptance suite. |
| `WHEREIS_BUILD_PROFILE` | `Release` | Default build type for single-configuration generators when `CMAKE_BUILD_TYPE` is not set. |
| `WHEREIS_ENABLE_IPO` | `ON` | Enable link-time optimization for Release builds when supported. |
| `WHEREIS_ENABLE_HARDENING` | `ON` | Enable supported hardening options, including SDL and Control Flow Guard for MSVC. |
| `WHEREIS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

### Testing

Run the acceptance tests with CTest:

```powershell
ctest --preset release
```

Or specify the build directory and configuration:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The tests use an isolated temporary directory tree and cover command-line parsing, binary discovery, manual and source searches, Unicode names, output encoding, and common search results.

Comparisons with `where.exe` are skipped when the corresponding command is not installed.

## Credits and License

### See Also

- `where.exe /?` — Windows command-line file locator.
- `Get-Command` — PowerShell command discovery.
- [`whereis(1)`](https://man.freebsd.org/cgi/man.cgi?query=whereis&sektion=1) — FreeBSD manual page.

### History

The original FreeBSD `whereis` was written by Jörg Wunsch in 2002 as a rewrite preserving the interface of the 4.3BSD utility.

The Windows port was written by Alfie Ardinata. It retains the original command-line interface and output format while adapting the search to Windows.

### License

This program is distributed under the BSD 2-Clause license, the license of the original FreeBSD source.
See [`LICENSE`](LICENSE) for the full text.

The original FreeBSD source is Copyright © 2002, Jörg Wunsch.

The Windows port is Copyright © 2026, Alfie Ardinata.

Retain all copyright notices, conditions, and disclaimers when redistributing the source or binary.
