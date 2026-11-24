cmake_minimum_required(VERSION 3.20)
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

if(FLAC_AX_USE_AX_SDK)
    include(FetchContent)
    set(AX_SDK_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk")
    if(EXISTS "${AX_SDK_LOCAL_DIR}/CMakeLists.txt")
        message(STATUS "FLAC_AX: Using local ax-sdk at ${AX_SDK_LOCAL_DIR}")
        add_subdirectory("${AX_SDK_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/_deps/ax_sdk-build" EXCLUDE_FROM_ALL)
    else()
        message(STATUS "FLAC_AX: Fetching pinned ax-sdk v1.36.2 from GitHub")
        FetchContent_Declare(
            ax_sdk
            GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
            GIT_TAG v1.36.2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(ax_sdk)
    endif()
endif()
```[cite: 8]

[FLAC_AX_CMakeLists_ax_sdk_pin.cmake](https://docs.google.com/document/d/1mV4DUD1qJuj99SBjO8SdZIj60is-R3g3J9hvN96lnGg/edit?usp=drivesdk&ouid=114366498032182118105)
