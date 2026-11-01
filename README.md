# whereis for Windows

`whereis` locates the binary, manual page, and source directory of a program on Windows.

It is derived from the `whereis(1)` utility of FreeBSD and keeps its command-line syntax and output format. The search logic is rewritten for Windows: it uses `PATH`, `PATHEXT`, the Registry, and the Win32 API instead of `sysctl(3)`, `man(1)`, and `locate(1)`. The program is written in C++23 and distributed as a single executable with the MSVC runtime statically linked.

`whereis` searches three categories:

- **Binary files**: executables, scripts, and Windows application execution aliases.
- **Manual pages and documentation**: man-page layouts and standalone documentation files.
- **Source directories**: directories named after the program, including repositories beneath configured source roots.

## Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Synopsis](#synopsis)
- [Description](#description)
- [How Files Are Found](#how-files-are-found)
- [Environment](#environment)
- [Output](#output)
- [Exit Status](#exit-status)
- [Examples](#examples)
- [Limitations](#limitations)
- [Differences from FreeBSD whereis](#differences-from-freebsd-whereis)
- [Building from Source](#building-from-source)
- [Testing](#testing)
- [See Also](#see-also)
- [History](#history)
- [License](#license)

## Installation

Download `whereis.exe` from the [latest release](https://github.com/relvinarsenio/whereis-windows/releases/latest). The executable has no installer and does not require the Visual C++ runtime.

A per-user installation needs no administrator privileges. Copy the executable to a directory of your choice, for example:

```text
%LOCALAPPDATA%\Programs\whereis\bin\whereis.exe
```

Add that directory to the user `PATH`:

```powershell
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\whereis\bin'
$UserPath   = [Environment]::GetEnvironmentVariable('Path', 'User')
$Entries    = @($UserPath -split ';' | Where-Object { $_.Trim() })

if ($Entries -notcontains $InstallDir) {
    [Environment]::SetEnvironmentVariable('Path', (($Entries + $InstallDir) -join ';'), 'User')
}

# Update PATH in the current session as well.
$env:Path = "$InstallDir;$env:Path"
```

New terminals inherit the updated `PATH`. To verify the installation:

```powershell
where.exe whereis
whereis.exe -b whereis
```

To uninstall, delete the installation directory and remove its entry from the user `PATH`.

To build the program yourself, see [Building from Source](#building-from-source).

## Quick Start

```powershell
whereis.exe pwsh
whereis.exe -b python winget wt
whereis.exe -h
```

Without a category option, `whereis` searches every category that has a usable search root. Use `-b` to search only for executables.

## Synopsis

```text
whereis [-abmqsuxh] [-B directory ...] [-M directory ...] [-S directory ... -f] name ...
whereis --help
```

Single-letter options may be combined: `-ab` is equivalent to `-a -b`. The directory options `-B`, `-M`, and `-S` must come last in a cluster, as in `-aB`.

A directory list ends at the next directory option, at `-f`, or at `--`. Use `-f` to mark the end of the last directory list and the beginning of the program names.

## Description

For each name, `whereis` searches the enabled categories in this order and prints what it finds:

1. Binary files.
2. Manual pages and documentation.
3. Source directories.

By default, the first match in each category is printed. If a name contains a path, only its final component is used. The suffixes `.bz2`, `.zst`, `.lzma`, `.gz`, `.xz`, `.Z`, and `.z` are removed from the name before searching.

The options are as follows:

| Option | Description |
|---|---|
| `-a` | Print all matches in each enabled category instead of the first. |
| `-b` | Search for binary files. |
| `-m` | Search for manual pages and documentation. |
| `-s` | Search for source directories. |
| `-B directory ...` | Search for binaries in the given directories instead of the default roots. |
| `-M directory ...` | Search for manual pages in the given directories instead of the default roots. |
| `-S directory ...` | Search for sources in the given directories instead of the default roots. |
| `-f` | End the directory lists. The remaining arguments are program names. |
| `-q` | Do not print the `name:` label. Print only the paths. |
| `-u` | Print only names for which an enabled category has no match. |
| `-x` | Skip the recursive source search. Direct `root\name` checks are still made. |
| `-h`, `--help` | Print a usage summary and exit. |
| `--` | End option processing. All following arguments are program names. |

Each directory argument may itself be a semicolon-separated list. The `-B`, `-M`, and `-S` options replace the default roots of their category; they do not add to them.

Quote any path that contains spaces. In PowerShell, use single or double quotes. In `cmd.exe`, use double quotes. Do not end a quoted path with a backslash: the sequence `\"` is read as a literal quotation mark. Write `"C:\Program Files\LLVM\bin"`, not `"C:\Program Files\LLVM\bin\"`.

### Default Categories

If none of `-b`, `-m`, and `-s` is given, all three are enabled. Windows has no standard location for manual pages or source trees, so a category that has no default root is disabled for that run. If no category remains, `whereis` reports an error.

A category selected explicitly with `-b`, `-m`, or `-s` must have at least one search directory. Set the matching environment variable or use the matching directory option; otherwise `whereis` reports an error.

## How Files Are Found

### Binary Files

Binary files are searched in the following order:

1. Each directory in `PATH`, in order.
2. The Windows system directory, normally `C:\Windows\System32`.
3. The Windows directory.
4. The Registry **App Paths** entries.

In each directory, the name is combined with each extension from `PATHEXT`. If `PATHEXT` is not set, a built-in list of common executable and script extensions is used. If the name already ends in a recognized executable extension, it is tried as given.

App Paths is a fallback and is consulted only after the directories. For each candidate file name, `HKEY_CURRENT_USER` is checked before `HKEY_LOCAL_MACHINE`, and both the 64-bit and 32-bit Registry views are read. The default value of

```text
Software\Microsoft\Windows\CurrentVersion\App Paths\<name>.exe
```

is used if it names an existing file.

Application execution aliases, such as those in `%LOCALAPPDATA%\Microsoft\WindowsApps`, are reparse points that the C++ standard library may not report as regular files. `whereis` tests candidates with `GetFileAttributesW` so that these aliases are found.

The current directory is not searched. Use `-B .` to search it alone, since `-B` replaces the default roots. Paths are printed with the file name case stored on disk.

### Manual Pages and Documentation

If `MANPATH` is set, its directories are the manual roots. Otherwise `whereis` uses the following directories, where they exist:

- for each `PATH` entry named `bin`: `..\share\man`, `..\usr\share\man`, and `..\..\usr\share\man`;
- `usr\share\man` below `MSYS2_ROOT`, `CYGWIN_ROOT`, and `GIT_INSTALL_ROOT`;
- `Git\usr\share\man` below `ProgramFiles` and `ProgramFiles(x86)`.

Within each root, these layouts are searched:

```text
root\
root\man\
root\doc\
root\docs\
root\share\man\
```

A page `name.section` is looked for as `manN\name.section` and directly in the layout directory. The sections are `1` to `9`, `1p`, and `3p`. A page may carry no suffix or one of `.gz`, `.bz2`, `.xz`, `.zst`, `.html`, and `.htm`.

Standalone documentation is also matched directly in the layout directory as `name.md`, `.markdown`, `.html`, `.htm`, `.pdf`, `.txt`, `.rst`, or `.adoc`.

This is a filesystem search. `whereis` does not run `man` and does not apply its formatting or section-selection rules.

### Source Directories

Each source root is first checked for a direct child directory named `name`. Unless `-x` is given, the roots are then searched recursively. Traversal descends at most eight levels below each root and does not enter directories named `.git`, `.hg`, `.svn`, `.vs`, `.cache`, `node_modules`, `packages`, `build`, `out`, or `obj`.

Unless `-S` is given, the source roots are the combination of the following:

- the directories listed in `SRCPATH` and `PORTSDIR`;
- `%VCPKG_ROOT%\ports`, if it exists;
- below `%USERPROFILE%`, where they exist: `source`, `source\repos`, `src`, `Documents\source`, `Documents\GitHub`, and `Documents\Visual Studio 2022\Projects` and `2026\Projects`;
- `src` and `source` in the root of `%SystemDrive%`, where they exist.

## Environment

Directory lists use the semicolon (`;`) as separator. In a directory argument or a list from the environment, `%NAME%` is expanded and a leading `~` stands for `%USERPROFILE%`.

| Variable | Used for | Description |
|---|---|---|
| `PATH` | Binaries, manuals | Binary search directories. Also used to locate default manual roots. |
| `PATHEXT` | Binaries | Extensions appended to the name. |
| `MANPATH` | Manuals | Manual and documentation roots. |
| `SRCPATH` | Sources | Source roots. |
| `PORTSDIR` | Sources | Additional source roots. |
| `VCPKG_ROOT` | Sources | The `ports` directory below this path is a source root. |
| `USERPROFILE` | Sources | Base of the default per-user source locations. |
| `SystemDrive` | Sources | Base of the default `src` and `source` roots. |
| `MSYS2_ROOT`, `CYGWIN_ROOT`, `GIT_INSTALL_ROOT` | Manuals | Their `usr\share\man` is a default manual root. |
| `ProgramFiles`, `ProgramFiles(x86)` | Manuals | Base of the default Git manual roots. |

## Output

Each name is printed on one line:

```text
name: binary-path manual-path source-path
```

Only the paths that were found appear. If nothing was found, the name and colon are still printed, unless `-q` is given:

```text
name:
```

A path that contains a space or a tab is enclosed in double quotes. With `-q`, the label is omitted and only the paths are printed.

If standard output is a console, the text is written as Unicode with CR LF line endings. If it is redirected to a file or pipe, the text is written as UTF-8 with LF line endings. A file name containing malformed UTF-16 is written with replacement characters.

## Exit Status

| Status | Meaning |
|---:|---|
| `0` | The command completed. A name that was not found is not an error. |
| `64` | Usage error, such as an unknown option or no program name. |
| `65` | No usable category or search directory. |

The values `64` and `65` correspond to `EX_USAGE` and `EX_DATAERR` in `sysexits.h`.

## Examples

Search every category that has a usable root:

```powershell
whereis.exe clang
```

Search only for binaries:

```powershell
whereis.exe -b clang git cmake
```

Print only the path of every binary match:

```powershell
whereis.exe -q -a -b pwsh
```

Search custom binary directories. The list ends at `-f`:

```powershell
whereis.exe -B 'C:\Program Files\LLVM\bin;D:\Tools\bin' -f clang
```

Search the manual pages of an MSYS2 installation:

```powershell
whereis.exe -m -M 'C:\msys64\usr\share\man' -f bash
```

Search two source roots without recursion:

```powershell
whereis.exe -s -x -S 'D:\src;C:\Users\me\source\repos' -f openssl
```

Set several directory lists in one command:

```powershell
whereis.exe -B 'C:\Tools;D:\Apps\bin' -M 'C:\msys64\usr\share\man' -S 'D:\src' -f git clang
```

List the names that have no binary:

```powershell
whereis.exe -u -b git cmake nonexistent-program
```

Treat a name that begins with a hyphen as a program name:

```powershell
whereis.exe -b -- -example
```

## Limitations

`whereis` is a filesystem-oriented locator. It does not reproduce how a shell resolves a command.

- PowerShell aliases, functions, and cmdlets are not resolved. Use `Get-Command`.
- AppX package manifests are not read. An execution alias is found only if its file exists in a searched directory.
- App Paths entries may point to locations that change when an application is updated.
- The current directory is not searched implicitly.
- A `.ps1` script is found only if `.PS1` is listed in `PATHEXT`.
- Manual and source results depend on the configured or detected roots. A program that is installed is not guaranteed to have a result.
- The recursive source search reports any directory with the requested name, so a common name such as `node` may match an unrelated directory. Use `-b` to search only for executables, or `-x` to skip recursion.
- A directory at the depth limit, or one of the pruned directories, is still compared with the requested name, but its contents are not visited.
- Redirected output is UTF-8. When PowerShell captures native program output as text, it must decode those bytes into .NET strings. In Windows PowerShell 5.1, `[Console]::OutputEncoding` may use a legacy code page, so non-ASCII paths can appear garbled. Set `[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()` before running `whereis`. PowerShell 7 uses UTF-8 by default; PowerShell 7.7 adds `$PSApplicationOutputEncoding` to configure decoding of native application output.
- PowerShell redirection behavior depends on the version. Windows PowerShell 5.1 writes redirected native command output as UTF-16LE, while PowerShell 7.0 through 7.3 writes it as UTF-8 without a byte order mark. Starting with PowerShell 7.4, redirection of native command stdout preserves the original byte stream. To preserve the exact UTF-8 bytes when using older versions, redirect in `cmd.exe`: `cmd /c "whereis.exe -b pwsh > out.txt"`.

## Differences from FreeBSD whereis

Users of the FreeBSD utility should note the following differences.

| Area | FreeBSD | Windows |
|---|---|---|
| Binary search roots | `user.cs_path`, `/usr/libexec`, then `PATH` | `PATH`, system directory, Windows directory, then App Paths |
| Executable test | Regular file with an execute bit | Existing file with an extension from `PATHEXT` |
| Manual search | Runs `man -w` | Probes the filesystem |
| Source search | Fixed `/usr/src` list and `/usr/ports` subdirectories; `locate` fallback | Configured roots; bounded recursive scan |
| Name matching | Case-sensitive | Case-insensitive |
| Name stripping | Leading path, `s.` prefix, `,v` suffix, `.z`, `.Z`, `.gz`, `.bz2` | Leading path; `.bz2`, `.zst`, `.lzma`, `.gz`, `.xz`, `.Z`, `.z` |
| Directory list separator | `:` | `;` |
| Paths with spaces | Printed as is | Enclosed in double quotes |
| Help option | None | `-h`, `--help` |

The exit status for a name that is not found, and the values `64` and `65`, are the same as in the FreeBSD utility.

## Building from Source

### Requirements

- Windows 10 or Windows 11.
- CMake 3.25 or later.
- A C++23 compiler and the Windows SDK, for example Visual Studio with the C++ workload.
- A build tool supported by CMake, such as a Visual Studio generator or Ninja.

### Build

The CMake presets do not select a generator, architecture, or toolset. CMake chooses its default generator; set `CMAKE_GENERATOR` to choose another.

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

A `debug` preset is also provided and builds in `build-debug`. To use Ninja:

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
cmake --preset release
```

Run the build from a Developer PowerShell or another environment in which the compiler and the Windows SDK are available.

With a multi-configuration generator such as Visual Studio, the executable is `build\Release\whereis.exe`. With a single-configuration generator such as Ninja, it is `build\whereis.exe`. When not using presets with a multi-configuration generator, pass `--config Release` to `cmake --build` and `-C Release` to `ctest`; otherwise the default configuration, commonly Debug, is built and tested.

To install without copying the file by hand:

```powershell
cmake --install build --config Release --prefix "$env:LOCALAPPDATA/Programs/whereis"
```

The executable is installed as `bin\whereis.exe` below the prefix.

### Build Options

The build sets target properties only and does not change global compiler flags.

| Option | Default | Description |
|---|---:|---|
| `BUILD_TESTING` | `ON` | Register the CTest acceptance suite. |
| `WHEREIS_BUILD_PROFILE` | `Release` | Build type for single-configuration generators when `CMAKE_BUILD_TYPE` is not set. |
| `WHEREIS_ENABLE_IPO` | `ON` | Enable link-time optimization for Release builds if the toolchain supports it. The program is a single translation unit, so the benefit is modest. |
| `WHEREIS_ENABLE_HARDENING` | `ON` | Enable supported hardening options: SDL and Control Flow Guard for MSVC, stack protection for GCC and Clang. |
| `WHEREIS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

## Testing

The acceptance tests are a CMake script run by CTest. They do not require PowerShell.

```powershell
ctest --preset release
ctest --test-dir build -C Release --output-on-failure
```

The tests build an isolated temporary tree and check:

- help output, a missing program name, and unknown options;
- names containing dots, paths containing spaces, and Unicode names;
- UTF-8 bytes in redirected output, and replacement of malformed UTF-16 file names;
- `-a`, `-q`, `-u`, `-x`, option clusters, `-f`, and `--`;
- `PATHEXT` handling and custom binary directories;
- sectioned manual pages and standalone documentation;
- direct and recursive source discovery;
- LF line endings in redirected output;
- agreement of the first result with `where.exe` for common commands such as `pwsh`, `python`, `winget`, `wt`, `find`, and `sort`.

The `where.exe` comparison is skipped for a command that is not installed. All other tests are mandatory.

## See Also

- `where.exe /?` and `Get-Command`, the Windows and PowerShell command locators.
- [`whereis(1)`](https://man.freebsd.org/cgi/man.cgi?query=whereis&sektion=1), the FreeBSD manual page.

## History

The FreeBSD `whereis` was written by Jörg Wunsch in 2002 as a from-scratch rewrite that preserves the user interface of the 4.3BSD utility. whereis for Windows takes that command-line interface and output format, and reimplements the search for the Win32 platform. The design and the original code belong to Jörg Wunsch and the FreeBSD Project; the Windows port was written by Alfie Ardinata.

## License

This program is distributed under the BSD 2-Clause license, the license of the original FreeBSD source. The source retains the original notice and Jörg Wunsch's 2002 copyright, together with the copyright of the Windows port. See [`LICENSE`](LICENSE) for the full text. Retain all copyright notices, conditions, and disclaimers when redistributing the source or the binary.