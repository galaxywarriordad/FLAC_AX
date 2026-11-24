cmake_minimum_required(VERSION 3.25)
project(FLAC_AX VERSION 1.36.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_BUILD_STUDIO "Build ax_av_studio live synth + OpenGL HUD" ON)
option(FLAC_AX_BUILD_UPSCALER "Build ax_video_upscaler DVD/HDMI video upscaler" ON)
option(FLAC_AX_BUILD_APPLIANCE_TUI "Build ax_appliance_tui Core 5 appliance launcher" ON)
option(FLAC_AX_BUILD_ENCODE_2GO "Build ax_encode_2go BATLEXP G350 video encoder launcher" ON)
option(FLAC_AX_USE_AX_SDK "Link ax_core (ax-sdk) for canonical 2:3:1 / Core 5 topology pinning" ON)
option(FLAC_AX_ENABLE_NATIVE_OPT "Enable -march=skylake / native tuning when supported" ON)
option(FLAC_AX_ENABLE_LTO "Enable Interprocedural Optimization (LTO) in Release builds" ON)
option(FLAC_AX_ENABLE_WARNINGS "Enable compiler warnings for FLAC_AX targets" ON)

if(FLAC_AX_USE_AX_SDK)
    include(FetchContent)
    set(AX_SDK_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk")
    if(EXISTS "${AX_SDK_LOCAL_DIR}/CMakeLists.txt")
        message(STATUS "FLAC_AX: Using local ax-sdk at ${AX_SDK_LOCAL_DIR}")
        add_subdirectory("${AX_SDK_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/_deps/ax_sdk-build" EXCLUDE_FROM_ALL)
    else()
        message(STATUS "FLAC_AX: Fetching pinned ax-sdk v1_36_2 from GitHub")
        FetchContent_Declare(
            ax_sdk
            GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
            GIT_TAG v1_36_2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(ax_sdk)
    endif()

    set(_flac_ax_clang_helper "${AX_SDK_LOCAL_DIR}/ax_toolchain/piccolo-clang-x86_64.cmake")
    if(NOT EXISTS "${_flac_ax_clang_helper}" AND DEFINED ax_sdk_SOURCE_DIR)
        set(_flac_ax_clang_helper "${ax_sdk_SOURCE_DIR}/ax_toolchain/piccolo-clang-x86_64.cmake")
    endif()
    if(EXISTS "${_flac_ax_clang_helper}")
        include("${_flac_ax_clang_helper}")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH=1 _DISABLE_VECTOR_ANNOTATION)
        if(FLAC_AX_ENABLE_NATIVE_OPT)
            add_compile_options(/clang:-O3 /clang:-march=skylake /clang:-mbmi /clang:-mbmi2)
        endif()
    else()
        if(FLAC_AX_ENABLE_NATIVE_OPT)
            add_compile_options(-O3 -march=skylake -mbmi -mbmi2)
        endif()
    endif()
endif()
