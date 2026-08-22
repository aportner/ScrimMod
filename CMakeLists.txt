cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Remove the generated CMake files from "
        "the repository root, then use ./build-linux.sh or configure with -B <build-dir>."
    )
endif()

project(ScrimMod VERSION 0.1.0 LANGUAGES CXX)

option(SCRIMMOD_BUILD_TESTS "Build platform-independent core tests" ON)
option(SCRIMMOD_BUILD_PLUGIN "Build the Linux Metamod-R plugin adapter" OFF)

include(cmake/dependencies.cmake)

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
set_target_properties(scrimmod_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

    include(FetchContent)
    FetchContent_Declare(
        metamod_r
        GIT_REPOSITORY https://github.com/rehlds/Metamod-R.git
        GIT_TAG ${SCRIMMOD_METAMOD_R_REVISION}
        GIT_PROGRESS TRUE
        SOURCE_SUBDIR scrimmod-no-cmake-project
    )
    FetchContent_MakeAvailable(metamod_r)

    # Keep every C++ target in the plugin binary on the legacy libstdc++ ABI used
    # throughout the ReHLDS ecosystem.
    target_compile_definitions(scrimmod_core PUBLIC _GLIBCXX_USE_CXX11_ABI=0)

    set(SCRIMMOD_HLSDK_ROOT
        "${metamod_r_SOURCE_DIR}/metamod/extra/example/include/hlsdk"
    )
    set(SCRIMMOD_METAMOD_INCLUDE
        "${metamod_r_SOURCE_DIR}/metamod/extra/example/include/metamod"
    )

    add_library(scrimmod_plugin SHARED src/plugin/metamod_plugin.cpp)
    target_link_libraries(scrimmod_plugin PRIVATE ScrimMod::Core dl)
    target_compile_features(scrimmod_plugin PRIVATE cxx_std_17)
    target_compile_definitions(scrimmod_plugin
        PRIVATE
            _GLIBCXX_USE_CXX11_ABI=0
            _LINUX
            LINUX
            NDEBUG
    )
    target_compile_options(scrimmod_plugin
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wno-write-strings
            -fno-sized-deallocation
            -fcf-protection=none
            -ffunction-sections
            -fdata-sections
    )
    target_include_directories(scrimmod_plugin
        SYSTEM PRIVATE
            "${SCRIMMOD_HLSDK_ROOT}/common"
            "${SCRIMMOD_HLSDK_ROOT}/dlls"
            "${SCRIMMOD_HLSDK_ROOT}/engine"
            "${SCRIMMOD_HLSDK_ROOT}/game_shared"
            "${SCRIMMOD_HLSDK_ROOT}/pm_shared"
            "${SCRIMMOD_HLSDK_ROOT}/public"
            "${SCRIMMOD_METAMOD_INCLUDE}"
    )
    target_link_options(scrimmod_plugin
        PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/exports.lds"
            "LINKER:--gc-sections"
    )
    set_target_properties(scrimmod_plugin
        PROPERTIES
            OUTPUT_NAME scrimmod_mm_i386
            PREFIX ""
            POSITION_INDEPENDENT_CODE ON
    )
    install(
        TARGETS scrimmod_plugin
        LIBRARY DESTINATION .
        COMPONENT plugin
    )
endif()
