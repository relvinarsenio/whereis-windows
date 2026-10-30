cmake_minimum_required(VERSION 3.25)

project(whereis
    DESCRIPTION "A Windows-native implementation of the BSD whereis utility"
    LANGUAGES CXX
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
    message(FATAL_ERROR "whereis is a Windows-native program and must be built on Windows.")
endif()

include(CheckIPOSupported)
include(CTest)
include(GNUInstallDirs)

# Choose a sensible default for single-configuration generators such as Ninja
# and Unix Makefiles. Multi-configuration generators (for example, Visual
# Studio) select Debug/Release at build and test time instead.
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

add_executable(whereis
    whereis.cpp
)

add_executable(whereis::whereis ALIAS whereis)

target_compile_features(whereis PRIVATE cxx_std_23)
target_compile_definitions(whereis PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(whereis PRIVATE Advapi32)

set_target_properties(whereis PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    OUTPUT_NAME whereis
)

# Keep warnings and compiler policy local to this target. clang-cl uses the
# MSVC command-line frontend even though its compiler ID is Clang.
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(whereis PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /EHsc
    )

    if(WHEREIS_ENABLE_HARDENING)
        target_compile_options(whereis PRIVATE /sdl /guard:cf)
        target_link_options(whereis PRIVATE
            /DYNAMICBASE
            /NXCOMPAT
            /GUARD:CF
        )
    endif()

    # CMake's standard Release flags already select /O2. These linker options
    # remove unreferenced COMDATs and fold identical ones in optimized builds.
    target_link_options(whereis PRIVATE
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
        "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
    )
else()
    target_compile_options(whereis PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
    )

    if(WHEREIS_ENABLE_HARDENING)
        target_compile_options(whereis PRIVATE
            "$<$<CONFIG:Release>:-fstack-protector-strong>"
        )
    endif()

    target_compile_options(whereis PRIVATE
        "$<$<CONFIG:Release>:-ffunction-sections>"
        "$<$<CONFIG:Release>:-fdata-sections>"
    )
    target_link_options(whereis PRIVATE
        "$<$<CONFIG:Release>:-Wl,--gc-sections>"
    )
endif()

if(WHEREIS_WARNINGS_AS_ERRORS)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(whereis PRIVATE /WX)
    else()
        target_compile_options(whereis PRIVATE -Werror)
    endif()
endif()

if(WHEREIS_ENABLE_IPO)
    check_ipo_supported(RESULT WHEREIS_IPO_SUPPORTED OUTPUT WHEREIS_IPO_DIAGNOSTIC LANGUAGES CXX)
    if(WHEREIS_IPO_SUPPORTED)
        set_property(TARGET whereis PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET whereis PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    else()
        message(STATUS "IPO/LTO is unavailable; optimized builds will use the compiler's normal Release optimization: ${WHEREIS_IPO_DIAGNOSTIC}")
    endif()
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
    add_executable(whereis_unicode_test tests/UnicodeTest.cpp)
    target_compile_features(whereis_unicode_test PRIVATE cxx_std_23)
    target_compile_definitions(whereis_unicode_test PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(whereis_unicode_test PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
            /EHsc
        )
        if(WHEREIS_WARNINGS_AS_ERRORS)
            target_compile_options(whereis_unicode_test PRIVATE /WX)
        endif()
    else()
        target_compile_options(whereis_unicode_test PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
        )
        if(WHEREIS_WARNINGS_AS_ERRORS)
            target_compile_options(whereis_unicode_test PRIVATE -Werror)
        endif()
    endif()

    add_test(
        NAME whereis.utf16
        COMMAND "$<TARGET_FILE:whereis_unicode_test>" "$<TARGET_FILE:whereis>"
    )
    set_tests_properties(whereis.utf16 PROPERTIES
        LABELS "windows;acceptance;unicode"
        TIMEOUT 30
        RUN_SERIAL TRUE
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
