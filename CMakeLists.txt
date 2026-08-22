cmake_minimum_required(VERSION 3.20)

project(ScrimMod VERSION 0.1.0 LANGUAGES CXX)

option(SCRIMMOD_BUILD_TESTS "Build platform-independent core tests" ON)
option(SCRIMMOD_BUILD_PLUGIN "Build the Linux Metamod-R plugin adapter" OFF)

add_library(scrimmod_core
    src/core/match_state.cpp
)
add_library(ScrimMod::Core ALIAS scrimmod_core)

target_include_directories(scrimmod_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(scrimmod_core PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scrimmod_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(SCRIMMOD_BUILD_TESTS)
    enable_testing()
    add_executable(scrimmod_core_tests tests/core/match_state_test.cpp)
    target_link_libraries(scrimmod_core_tests PRIVATE ScrimMod::Core)
    target_compile_features(scrimmod_core_tests PRIVATE cxx_std_17)
    add_test(NAME scrimmod.core COMMAND scrimmod_core_tests)
endif()

if(SCRIMMOD_BUILD_PLUGIN)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The ScrimMod plugin target is supported only on Linux")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "The ScrimMod plugin must be compiled with GCC")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "The ScrimMod plugin must be configured with a 32-bit toolchain")
    endif()

    message(FATAL_ERROR
        "The plugin adapter is intentionally gated until pinned ReHLDS, ReGameDLL_CS, "
        "and Metamod-R dependency revisions are recorded"
    )
endif()
