cmake_minimum_required(VERSION 3.24)
project(flac_ax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_USE_AX_SDK "Fetch and link ax-sdk (v1_36_1)" ON)
option(FLAC_AX_BUILD_AV_STUDIO "Build ax_av_studio (MPK Mini MK3 + AVX2 Synth + A/V HUD)" ON)
option(FLAC_AX_BUILD_VIDEO_UPSCALER "Build single-stream 720p/1080p/2K DVD & V4L2 retro upscaler (ax_video_upscaler)" ON)
option(FLAC_AX_BUILD_APPLIANCE_TUI "Build Core 5 Espresso Real-Time Appliance TUI (ax_appliance_tui)" ON)
option(FLAC_AX_ENABLE_OPENMPT "Enable Amiga/Atari ST tracker module playback (.mod/.xm/.s3m/.it) via libopenmpt" ON)
option(FLAC_AX_ENABLE_SDL2_GL "Enable SDL2 + OpenGL window output when available" ON)
option(FLAC_AX_SKYLAKE_ARCH "Target Skylake microarchitecture (-march=skylake / /arch:AVX2)" ON)
option(FLAC_AX_ENABLE_LTO "Enable Link-Time Optimization (LLVM LTO / IPO) in Release builds" ON)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
endif()

set(_flac_ax_has_openmpt FALSE)
if(UNIX AND NOT APPLE AND FLAC_AX_ENABLE_OPENMPT)
    pkg_check_modules(_FLAC_AX_OPENMPT QUIET IMPORTED_TARGET libopenmpt)
    if(_FLAC_AX_OPENMPT_FOUND)
        set(_flac_ax_has_openmpt TRUE)
    endif()
endif()

set(_flac_ax_has_sdl2_gl FALSE)
if(UNIX AND NOT APPLE AND FLAC_AX_ENABLE_SDL2_GL)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    find_package(OpenGL QUIET)
    if(SDL2_FOUND AND TARGET OpenGL::GL)
        set(_flac_ax_has_sdl2_gl TRUE)
    endif()
endif()

if(FLAC_AX_USE_AX_SDK)
    if(NOT TARGET ax::core)
        find_package(ax-sdk CONFIG QUIET)
    endif()
    if(NOT TARGET ax::core)
        if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_AX_SDK)
            if(DEFINED ENV{AX_SDK_DIR} AND EXISTS "$ENV{AX_SDK_DIR}/CMakeLists.txt")
                set(FETCHCONTENT_SOURCE_DIR_AX_SDK "$ENV{AX_SDK_DIR}" CACHE PATH "" FORCE)
            elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk/CMakeLists.txt")
                set(FETCHCONTENT_SOURCE_DIR_AX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk" CACHE PATH "" FORCE)
            endif()
        endif()

        if(DEFINED FETCHCONTENT_SOURCE_DIR_AX_SDK AND EXISTS "${FETCHCONTENT_SOURCE_DIR_AX_SDK}/CMakeLists.txt")
            add_subdirectory(
                "${FETCHCONTENT_SOURCE_DIR_AX_SDK}"
                "${CMAKE_CURRENT_BINARY_DIR}/_deps/ax_sdk-build"
            )
        else()
            include(FetchContent)
            set(_ax_proto "https")
            set(_ax_host "github.com")
            set(_ax_repo "galaxywarriordad/ax-sdk.git")
            FetchContent_Declare(
                ax_sdk
                GIT_REPOSITORY "${_ax_proto}://${_ax_host}/${_ax_repo}"
                GIT_TAG        v1_36_1
                GIT_SHALLOW    FALSE
            )
            FetchContent_MakeAvailable(ax_sdk)
        endif()
    endif()
endif()

set(FLAC_AX_DSD_HEADERS
    src/dsd_dop_reader.hpp
    src/tracker_openmpt_reader.hpp
)

set(FLAC_AX_DSP_STUDIO_HEADERS
    src/audio/wav_bounce_recorder.hpp
    src/dsp/full_feature_synth.hpp
    src/dsp/stereo_reverb_dsp.hpp
    src/dsp/tape_delay_dsp.hpp
    src/midi/mpk_mini3_oled.hpp
)

set(FLAC_AX_VIDEO_HEADERS
    src/video/ax_720p_hw_gate.hpp
    src/video/v4l2_live_capture.hpp
    src/video/upscale_720p_shader.hpp
    src/video/av_pts_sync.hpp
)

set(FLAC_AX_TUI_HEADERS
    src/tui/appliance_tui.hpp
)

function(flac_ax_apply_target_flags target_name)
    target_include_directories(${target_name} PRIVATE src)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:/O2 -DNDEBUG>
            $<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_SKYLAKE_ARCH}>>:/clang:-march=skylake>
            -Wall -Wextra -Wpedantic
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>
            $<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_SKYLAKE_ARCH}>>:-march=skylake>
            -Wall -Wextra -Wpedantic
        )
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:/O2 /DNDEBUG>
            $<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_SKYLAKE_ARCH}>>:/arch:AVX2>
            /W3
        )
    else()
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>
            $<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_SKYLAKE_ARCH}>>:-march=skylake>
            -Wall -Wextra -Wpedantic
        )
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(${target_name} PRIVATE pthread)
    endif()

    if(FLAC_AX_USE_AX_SDK)
        target_link_libraries(${target_name} PRIVATE ax::core)
        target_compile_definitions(${target_name} PRIVATE FLAC_AX_HAS_AX_SDK=1)
    endif()
endfunction()

# --- 1. Headless Bit-Perfect ESS Sabre FLAC, DoP & Tracker Module Player ---
add_executable(flac_ax
    src/main.cpp
    ${FLAC_AX_DSD_HEADERS}
)
flac_ax_apply_target_flags(flac_ax)
if(UNIX AND NOT APPLE)
    target_link_libraries(flac_ax PRIVATE PkgConfig::FLAC PkgConfig::ALSA)
    if(_flac_ax_has_openmpt)
        target_link_libraries(flac_ax PRIVATE PkgConfig::_FLAC_AX_OPENMPT)
        target_compile_definitions(flac_ax PRIVATE FLAC_AX_HAS_OPENMPT=1)
    endif()
endif()

# --- 2. Unified [AX] Audio/Visual Synth, Mixer & Spectrum Studio ---
if(FLAC_AX_BUILD_AV_STUDIO)
    if(UNIX AND NOT APPLE AND NOT _flac_ax_has_sdl2_gl)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        find_package(OpenGL REQUIRED)
    endif()

    add_executable(ax_av_studio
        src/studio_main.cpp
        src/midi/mpk_mini3.cpp
        src/dsp/avx2_synth.cpp
        src/visual/av_hud.cpp
        ${FLAC_AX_DSP_STUDIO_HEADERS}
    )
    flac_ax_apply_target_flags(ax_av_studio)
    if(UNIX AND NOT APPLE)
        target_link_libraries(ax_av_studio PRIVATE
            PkgConfig::ALSA
            PkgConfig::FLAC
            PkgConfig::SDL2
            OpenGL::GL
        )
        target_compile_definitions(ax_av_studio PRIVATE FLAC_AX_HAS_SDL2_GL=1)
    endif()
endif()

# --- 3. Single-Stream 720p/1080p/2K DVD & V4L2 Retro Upscaler ---
if(FLAC_AX_BUILD_VIDEO_UPSCALER)
    add_executable(ax_video_upscaler
        src/video/video_upscaler_main.cpp
        ${FLAC_AX_VIDEO_HEADERS}
    )
    flac_ax_apply_target_flags(ax_video_upscaler)
    if(UNIX AND NOT APPLE)
        target_link_libraries(ax_video_upscaler PRIVATE PkgConfig::ALSA)
        if(_flac_ax_has_sdl2_gl)
            target_link_libraries(ax_video_upscaler PRIVATE PkgConfig::SDL2 OpenGL::GL)
            target_compile_definitions(ax_video_upscaler PRIVATE FLAC_AX_HAS_SDL2_GL=1)
        endif()
    endif()
endif()

# --- 4. Core 5 Espresso Real-Time Appliance Launcher TUI ---
if(UNIX AND NOT APPLE AND FLAC_AX_BUILD_APPLIANCE_TUI)
    add_executable(ax_appliance_tui
        src/appliance_tui_main.cpp
        ${FLAC_AX_TUI_HEADERS}
    )
    flac_ax_apply_target_flags(ax_appliance_tui)
endif()

if(FLAC_AX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET flac_ax PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(TARGET ax_av_studio)
            set_property(TARGET ax_av_studio PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
        if(TARGET ax_video_upscaler)
            set_property(TARGET ax_video_upscaler PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
        if(TARGET ax_appliance_tui)
            set_property(TARGET ax_appliance_tui PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
endif()
