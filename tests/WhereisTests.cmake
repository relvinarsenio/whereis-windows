
# CTest acceptance suite for the Windows whereis executable.
# Run through CTest; this script intentionally uses CMake rather than PowerShell.

if(NOT DEFINED WHEREIS_EXE OR NOT EXISTS "${WHEREIS_EXE}")
    message(FATAL_ERROR "WHEREIS_EXE must name an existing whereis executable.")
endif()

if(NOT DEFINED WHEREIS_TEST_ROOT)
    message(FATAL_ERROR "WHEREIS_TEST_ROOT was not provided by CTest.")
endif()

file(REMOVE_RECURSE "${WHEREIS_TEST_ROOT}")
file(MAKE_DIRECTORY "${WHEREIS_TEST_ROOT}")

function(pass label)
    message(STATUS "[PASS] ${label}")
endfunction()

function(run_whereis expected_status label)
    execute_process(
        COMMAND "${WHEREIS_EXE}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        ENCODING UTF-8
        TIMEOUT 20
    )

    if(NOT "${_result}" STREQUAL "${expected_status}")
        message(FATAL_ERROR
            "${label}: expected exit ${expected_status}, got '${_result}'.\n"
            "stdout:\n${_stdout}\nstderr:\n${_stderr}")
    endif()

    set(WHEREIS_OUTPUT "${_stdout}" PARENT_SCOPE)
    set(WHEREIS_ERROR "${_stderr}" PARENT_SCOPE)
    set(WHEREIS_RESULT "${_result}" PARENT_SCOPE)
endfunction()

# Windows paths and executable extensions are normally case-insensitive.
# Preserve the actual on-disk casing in program output, but compare assertions
# without case sensitivity.

function(assert_contains haystack needle label)
    string(TOLOWER "${haystack}" _haystack_lower)
    string(TOLOWER "${needle}" _needle_lower)

    string(FIND "${_haystack_lower}" "${_needle_lower}" _position)

    if(_position EQUAL -1)
        message(FATAL_ERROR
            "${label}: expected output to contain '${needle}' (case-insensitive).\n"
            "Output:\n${haystack}")
    endif()

    pass("${label}")
endfunction()

function(assert_not_contains haystack needle label)
    string(TOLOWER "${haystack}" _haystack_lower)
    string(TOLOWER "${needle}" _needle_lower)

    string(FIND "${_haystack_lower}" "${_needle_lower}" _position)

    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "${label}: output unexpectedly contains '${needle}' (case-insensitive).\n"
            "Output:\n${haystack}")
    endif()

    pass("${label}")
endfunction()

# Help and command-line errors.

run_whereis(0 "help" --help)
assert_contains("${WHEREIS_OUTPUT}" "Usage: whereis [-abmqsuxh]" "help synopsis lists supported short options")
assert_contains("${WHEREIS_OUTPUT}" "whereis --help" "help synopsis documents --help")
assert_contains("${WHEREIS_OUTPUT}" "-h, --help" "help documents both help options")
assert_contains("${WHEREIS_OUTPUT}" "Stop option processing" "help documents --")

run_whereis(0 "short help option" -h)
assert_contains("${WHEREIS_OUTPUT}" "Usage: whereis" "-h prints usage")

run_whereis(64 "no arguments")
assert_contains("${WHEREIS_ERROR}" "No program names" "missing query is a usage error")

run_whereis(64 "unknown option" --not-a-real-option sample)
assert_contains("${WHEREIS_ERROR}" "Unknown option" "unknown option is rejected")

# Binary lookup: dotted names, paths with spaces, all matches and quiet output.

set(_binary_one "${WHEREIS_TEST_ROOT}/binary one")
set(_binary_two "${WHEREIS_TEST_ROOT}/binary-two")

file(MAKE_DIRECTORY "${_binary_one}" "${_binary_two}")

file(WRITE "${_binary_one}/whereis-cmake-foo-1.2.exe" "test")
file(WRITE "${_binary_two}/whereis-cmake-foo-1.2.exe" "test")

run_whereis(0 "dotted executable name"
    -B "${_binary_one}" -f whereis-cmake-foo-1.2)

assert_contains("${WHEREIS_OUTPUT}" "whereis-cmake-foo-1.2.exe"
    "a dotted basename is treated as part of the name")

assert_contains("${WHEREIS_OUTPUT}" "\""
    "paths containing spaces are quoted")

run_whereis(0 "all binary matches"
    -abB "${_binary_one}" "${_binary_two}" -f whereis-cmake-foo-1.2)

string(TOLOWER "${WHEREIS_OUTPUT}" _all_binary_output_lower)

string(REGEX MATCHALL
    "whereis-cmake-foo-1\\.2\\.exe"
    _binary_hits
    "${_all_binary_output_lower}"
)

list(LENGTH _binary_hits _binary_hit_count)

if(NOT _binary_hit_count EQUAL 2)
    message(FATAL_ERROR
        "-a should return both test executables; got ${_binary_hit_count}.\n"
        "${WHEREIS_OUTPUT}")
endif()

pass("-a returns all binary matches")

# Both command-line arguments and redirected output use Unicode-safe paths.

set(_unicode_dir "${WHEREIS_TEST_ROOT}/unicode 路径")

file(MAKE_DIRECTORY "${_unicode_dir}")
file(WRITE "${_unicode_dir}/工具.exe" "test")

run_whereis(0 "Unicode query and path"
    -b -B "${_unicode_dir}" -f 工具)

assert_contains("${WHEREIS_OUTPUT}" "工具.exe"
    "Unicode query and filename are preserved")

set(_unicode_output_file "${WHEREIS_TEST_ROOT}/unicode-output.bin")
execute_process(
    COMMAND "${WHEREIS_EXE}" -q -b -B "${_unicode_dir}" -f 工具
    RESULT_VARIABLE _unicode_result
    OUTPUT_FILE "${_unicode_output_file}"
    ERROR_VARIABLE _unicode_error
    TIMEOUT 20
)
if(NOT "${_unicode_result}" STREQUAL "0")
    message(FATAL_ERROR "Unicode redirected-output test failed: ${_unicode_error}")
endif()

file(READ "${_unicode_output_file}" _unicode_hex HEX)
string(TOLOWER "${_unicode_hex}" _unicode_hex)
string(HEX "工具.exe" _unicode_filename_hex)
string(HEX "unicode 路径" _unicode_directory_hex)
string(TOLOWER "${_unicode_filename_hex}" _unicode_filename_hex)
string(TOLOWER "${_unicode_directory_hex}" _unicode_directory_hex)

if(NOT _unicode_hex MATCHES "${_unicode_filename_hex}")
    message(FATAL_ERROR
        "Redirected output does not contain the filename as UTF-8. Bytes: ${_unicode_hex}")
endif()
if(NOT _unicode_hex MATCHES "${_unicode_directory_hex}")
    message(FATAL_ERROR
        "Redirected output does not contain the directory as UTF-8. Bytes: ${_unicode_hex}")
endif()
if(NOT _unicode_hex MATCHES "0a$" OR _unicode_hex MATCHES "0d0a$")
    message(FATAL_ERROR "Unicode redirected output must end with LF, not CRLF. Bytes: ${_unicode_hex}")
endif()
if(_unicode_hex MATCHES "efbfbd")
    message(FATAL_ERROR "Valid Unicode output unexpectedly contains U+FFFD. Bytes: ${_unicode_hex}")
endif()
pass("redirected Unicode paths are encoded as UTF-8")

# Quiet output.

run_whereis(0 "quiet binary output"
    -q -b -B "${_binary_one}" -f whereis-cmake-foo-1.2)

assert_not_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-foo-1.2:"
    "-q suppresses the name label")

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-foo-1.2.exe"
    "-q retains the result path")

# PATHEXT is read from the process environment and PATH is searched before fallbacks.

set(_pathext_dir "${WHEREIS_TEST_ROOT}/pathext")

file(MAKE_DIRECTORY "${_pathext_dir}")
file(WRITE "${_pathext_dir}/whereis-cmake-pathext.cmd" "test")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${_pathext_dir}"
        "PATHEXT=.CMD"
        "${WHEREIS_EXE}" -b whereis-cmake-pathext
    RESULT_VARIABLE _pathext_result
    OUTPUT_VARIABLE _pathext_output
    ERROR_VARIABLE _pathext_error
    ENCODING UTF-8
    TIMEOUT 20
)

string(TOLOWER "${_pathext_output}" _pathext_output_lower)

if(NOT "${_pathext_result}" STREQUAL "0" OR
   NOT "${_pathext_output_lower}" MATCHES "whereis-cmake-pathext\\.cmd")
    message(FATAL_ERROR
        "PATHEXT lookup failed.\n"
        "stdout:\n${_pathext_output}\n"
        "stderr:\n${_pathext_error}")
endif()

pass("PATHEXT controls executable extension lookup")

# The -- marker permits a query beginning with a dash.

file(WRITE "${_binary_one}/-whereis-cmake-leading.exe" "test")

run_whereis(0 "end of options marker"
    -B "${_binary_one}" -- -whereis-cmake-leading)

assert_contains("${WHEREIS_OUTPUT}"
    "-whereis-cmake-leading.exe"
    "-- ends option parsing")

# BSD unusual semantics: print names missing at least one requested category.

run_whereis(0 "unusual with one missing name"
    -u -b cmd whereis-cmake-absent)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-absent:"
    "-u includes a missing name")

assert_not_contains("${WHEREIS_OUTPUT}"
    "cmd:"
    "-u suppresses a fully found name")

run_whereis(0 "unusual with only a found name" -u -b cmd)

if(NOT "${WHEREIS_OUTPUT}" STREQUAL "")
    message(FATAL_ERROR
        "-u should suppress a name found in every requested category.\n"
        "${WHEREIS_OUTPUT}")
endif()

pass("-u suppresses a fully found name")

# Clustered BSD options and -f handling.

run_whereis(0 "-ab cluster" -ab cmd)
assert_contains("${WHEREIS_OUTPUT}" "cmd:" "-ab cluster is accepted")

run_whereis(0 "-abf cluster" -abf cmd)
assert_contains("${WHEREIS_OUTPUT}" "cmd:" "-abf cluster is accepted")

run_whereis(0 "-baf cluster" -baf cmd)
assert_contains("${WHEREIS_OUTPUT}" "cmd:" "-baf cluster is accepted")

run_whereis(0 "-aB cluster"
    -aB "${_binary_one}" -f whereis-cmake-foo-1.2)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-foo-1.2.exe"
    "-aB directory cluster and -f terminator are accepted")

# Manual page and standalone documentation discovery.

set(_manual_root "${WHEREIS_TEST_ROOT}/manual")

file(MAKE_DIRECTORY
    "${_manual_root}/man1"
    "${_manual_root}/docs"
)

file(WRITE "${_manual_root}/man1/whereis-cmake-manual.1" "manual test")
file(WRITE "${_manual_root}/docs/whereis-cmake-doc.md" "documentation test")

run_whereis(0 "manual page lookup"
    -m -M "${_manual_root}" -f whereis-cmake-manual)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-manual.1"
    "-m discovers a sectioned manual page")

run_whereis(0 "documentation lookup"
    -m -M "${_manual_root}" -f whereis-cmake-doc)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-doc.md"
    "-m discovers standalone documentation")

# Source lookup: direct child, recursive child and -x suppression.

set(_source_root "${WHEREIS_TEST_ROOT}/source")

file(MAKE_DIRECTORY
    "${_source_root}/whereis-cmake-direct"
    "${_source_root}/nested/repository/whereis-cmake-recursive"
)

run_whereis(0 "direct source lookup"
    -s -S "${_source_root}" -f whereis-cmake-direct)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-direct"
    "-s finds a direct source directory")

run_whereis(0 "recursive source lookup"
    -s -S "${_source_root}" -f whereis-cmake-recursive)

assert_contains("${WHEREIS_OUTPUT}"
    "whereis-cmake-recursive"
    "-s discovers a nested source directory")

run_whereis(0 "non-recursive source lookup"
    -s -x -S "${_source_root}" -f whereis-cmake-recursive)

if(WHEREIS_OUTPUT MATCHES "whereis-cmake-recursive:[ \t]+")
    message(FATAL_ERROR
        "-x unexpectedly returned a recursively discovered source path.\n"
        "${WHEREIS_OUTPUT}")
endif()

pass("-x skips recursive source discovery")

# Redirected output must be UTF-8 with LF line endings, not CRLF.

set(_redirected_file "${WHEREIS_TEST_ROOT}/redirected-output.txt")

execute_process(
    COMMAND "${WHEREIS_EXE}" -q -b cmd
    RESULT_VARIABLE _redirect_result
    OUTPUT_FILE "${_redirected_file}"
    ERROR_VARIABLE _redirect_error
    TIMEOUT 20
)

if(NOT "${_redirect_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Redirected-output test failed: ${_redirect_error}")
endif()

file(READ "${_redirected_file}" _redirected_hex HEX)
string(TOLOWER "${_redirected_hex}" _redirected_hex)

if(NOT _redirected_hex MATCHES "0a$")
    message(FATAL_ERROR
        "Redirected output does not end with LF. Bytes: ${_redirected_hex}")
endif()

if(_redirected_hex MATCHES "0d0a$")
    message(FATAL_ERROR
        "Redirected output ends with CRLF instead of LF.")
endif()

pass("redirected output ends with LF and not CRLF")

# Compare the first result for common system binaries with Microsoft's where.exe.
# Windows path comparison is case-insensitive.

if(DEFINED WHEREIS_WHERE_EXE AND EXISTS "${WHEREIS_WHERE_EXE}")

    foreach(_name IN ITEMS pwsh python winget wt find sort)

        execute_process(
            COMMAND "${WHEREIS_WHERE_EXE}" "${_name}"
            RESULT_VARIABLE _where_result
            OUTPUT_VARIABLE _where_output
            ERROR_QUIET
            ENCODING UTF-8
            TIMEOUT 20
        )

        if("${_where_result}" STREQUAL "0")

            string(REGEX MATCH "^[^\r\n]+" _where_first "${_where_output}")

            run_whereis(0 "whereis lookup for ${_name}" -b "${_name}")

            string(REGEX MATCH
                "^[^\r\n]+"
                _whereis_first_line
                "${WHEREIS_OUTPUT}"
            )

            string(REPLACE
                "${_name}: "
                ""
                _whereis_first
                "${_whereis_first_line}"
            )

            string(REGEX REPLACE
                "^\"(.*)\"$"
                "\\1"
                _whereis_first
                "${_whereis_first}"
            )

            string(TOLOWER "${_where_first}" _where_first)
            string(TOLOWER "${_whereis_first}" _whereis_first)

            if(NOT "${_where_first}" STREQUAL "${_whereis_first}")
                message(FATAL_ERROR
                    "First result differs from where.exe for ${_name}.\n"
                    "where.exe: ${_where_first}\n"
                    "whereis: ${_whereis_first}")
            endif()

            pass("first ${_name} result agrees with where.exe")

        else()

            message(STATUS
                "[SKIP] where.exe cannot resolve ${_name} on this host")

        endif()

    endforeach()

else()

    message(STATUS
        "[SKIP] where.exe was not found; system-result comparison omitted")

endif()

file(REMOVE_RECURSE "${WHEREIS_TEST_ROOT}")
message(STATUS "whereis CTest acceptance suite completed successfully.")
