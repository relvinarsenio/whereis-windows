cmake_minimum_required(VERSION 3.25)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(whereis
    DESCRIPTION "A Windows-native implementation of the BSD whereis utility"
    LANGUAGES CXX
)

if(NOT WIN32 OR NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR
        "whereis is a Windows-native program and must be built on Windows.")
endif()

if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR
        (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
         CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")))
    message(FATAL_ERROR
        "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID} "
        "(frontend: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}). "
        "Supported compilers are MSVC (cl.exe) and Clang-CL (clang-cl.exe).")
endif()

include(CheckIPOSupported)
include(CTest)
include(GNUInstallDirs)

set(WHEREIS_BUILD_PROFILE "Release" CACHE STRING
    "Default configuration for single-configuration generators")
set_property(CACHE WHEREIS_BUILD_PROFILE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

if(NOT WHEREIS_BUILD_PROFILE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
    message(FATAL_ERROR
        "WHEREIS_BUILD_PROFILE must be Debug, Release, RelWithDebInfo, or MinSizeRel.")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "${WHEREIS_BUILD_PROFILE}" CACHE STRING
        "Build type for single-configuration generators" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(WHEREIS_ENABLE_IPO "Enable interprocedural optimization for optimized builds" ON)
option(WHEREIS_ENABLE_HARDENING "Enable supported compiler and linker hardening options" ON)
option(WHEREIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(whereis_compiler_flags INTERFACE)

target_compile_features(whereis_compiler_flags INTERFACE cxx_std_23)
target_compile_definitions(whereis_compiler_flags INTERFACE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(whereis_compiler_flags INTERFACE
    /W4
    /permissive-
    /utf-8
    /EHsc
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(whereis_compiler_flags INTERFACE
        /Zc:__cplusplus
        /Zc:preprocessor
    )
endif()

if(WHEREIS_WARNINGS_AS_ERRORS)
    target_compile_options(whereis_compiler_flags INTERFACE /WX)
endif()

set(WHEREIS_PCH_HEADERS
    <windows.h>
    <winreg.h>
    <algorithm>
    <filesystem>
    <format>
    <ranges>
    <string>
    <vector>
)

add_executable(whereis whereis.cpp)
add_executable(whereis::whereis ALIAS whereis)

target_link_libraries(whereis PRIVATE
    whereis_compiler_flags
    Advapi32
)

target_precompile_headers(whereis PRIVATE ${WHEREIS_PCH_HEADERS})

set_target_properties(whereis PROPERTIES
    CXX_EXTENSIONS NO
)

if(WHEREIS_ENABLE_HARDENING)
    target_compile_options(whereis PRIVATE /guard:cf)
    target_link_options(whereis PRIVATE
        /DYNAMICBASE
        /NXCOMPAT
        /GUARD:CF
    )
endif()

if(WHEREIS_ENABLE_HARDENING AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(whereis PRIVATE /sdl)
endif()

target_compile_options(whereis PRIVATE
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Gw>"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1935)
    target_compile_options(whereis PRIVATE
        "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Zc:checkGwOdr>"
    )
endif()

target_link_options(whereis PRIVATE
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/OPT:REF>"
    "$<$<CONFIG:Release,MinSizeRel>:/OPT:ICF>"
    "$<$<CONFIG:RelWithDebInfo>:/OPT:NOICF>"
)

if(WHEREIS_ENABLE_IPO)
    check_ipo_supported(
        RESULT WHEREIS_IPO_SUPPORTED
        OUTPUT WHEREIS_IPO_DIAGNOSTIC
        LANGUAGES CXX
    )
endif()

if(WHEREIS_ENABLE_IPO AND WHEREIS_IPO_SUPPORTED)
    set_property(TARGET whereis PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET whereis PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

if(WHEREIS_ENABLE_IPO AND NOT WHEREIS_IPO_SUPPORTED)
    message(STATUS
        "IPO/LTO is unavailable; optimized builds will use the compiler's normal optimization: ${WHEREIS_IPO_DIAGNOSTIC}")
endif()

install(TARGETS whereis
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

install(FILES README.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT Documentation
)

if(BUILD_TESTING)
    include(FetchContent)

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_build_tests OFF CACHE BOOL "" FORCE)
    set(gtest_build_samples OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 063de7e9578f82b369302001269680b4b1553359
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(whereis_unicode_test tests/UnicodeTest.cpp)

    target_link_libraries(whereis_unicode_test PRIVATE
        whereis_compiler_flags
        GTest::gtest_main
    )

    target_precompile_headers(whereis_unicode_test PRIVATE ${WHEREIS_PCH_HEADERS})

    add_executable(whereis_unit_test tests/WhereisUnitTest.cpp)

    target_link_libraries(whereis_unit_test PRIVATE
        whereis_compiler_flags
        GTest::gtest_main
        Advapi32
    )

    target_precompile_headers(whereis_unit_test PRIVATE ${WHEREIS_PCH_HEADERS})

    include(GoogleTest)

    gtest_discover_tests(whereis_unit_test
        DISCOVERY_TIMEOUT 15
        PROPERTIES
            LABELS "windows;unit;internal"
            TIMEOUT 30
    )

    gtest_discover_tests(whereis_unicode_test
        DISCOVERY_TIMEOUT 15
        PROPERTIES
            LABELS "windows;integration;unicode"
            TIMEOUT 30
            SKIP_REGULAR_EXPRESSION "SKIPPED"
            ENVIRONMENT "WHEREIS_EXE=$<TARGET_FILE:whereis>"
    )

    find_program(WHEREIS_WHERE_EXE NAMES where.exe)

    add_test(
        NAME whereis.acceptance
        COMMAND "${CMAKE_COMMAND}"
            "-DWHEREIS_EXE=$<TARGET_FILE:whereis>"
            "-DWHEREIS_WHERE_EXE=${WHEREIS_WHERE_EXE}"
            "-DWHEREIS_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/whereis"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/WhereisTests.cmake"
    )

    set_tests_properties(whereis.acceptance PROPERTIES
        LABELS "windows;acceptance"
        TIMEOUT 60
        RUN_SERIAL TRUE
    )
endif()
