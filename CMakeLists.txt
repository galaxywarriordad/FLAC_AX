cmake_minimum_required(VERSION 3.24)
project(flac_ax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_USE_AX_SDK "Fetch and link ax-sdk (v1_36_1)" ON)
option(FLAC_AX_BUILD_AV_STUDIO "Build ax_av_studio (MPK Mini MK3 + AVX2 Synth + A/V HUD)" ON)
option(FLAC_AX_NATIVE_ARCH "Optimize for host CPU (-march=native) on bare-metal target" OFF)
option(FLAC_AX_ENABLE_LTO "Enable Link-Time Optimization (IPO/LTO) in Release builds" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

if(FLAC_AX_USE_AX_SDK)
    include(FetchContent)
    set(_ax_proto "https")
    set(_ax_host "github.com")
    set(_ax_repo "galaxywarriordad/ax-sdk.git")
    FetchContent_Declare(
        ax_sdk
        GIT_REPOSITORY "${_ax_proto}://${_ax_host}/${_ax_repo}"
        GIT_TAG        v1_36_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ax_sdk)
endif()

set(FLAC_AX_DSD_HEADERS
    src/dsd_dop_reader.hpp
)

set(FLAC_AX_DSP_STUDIO_HEADERS
    src/dsp/full_feature_synth.hpp
    src/dsp/stereo_reverb_dsp.hpp
    src/dsp/tape_delay_dsp.hpp
    src/midi/mpk_mini3_oled.hpp
)

# --- 1. Headless Bit-Perfect ESS Sabre FLAC Player ---
add_executable(flac_ax
    src/main.cpp
    ${FLAC_AX_DSD_HEADERS}
)
target_include_directories(flac_ax PRIVATE src)

target_compile_options(flac_ax PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>$<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_NATIVE_ARCH}>>:-march=native>
    -Wall -Wextra -Wpedantic
)

target_link_libraries(flac_ax PRIVATE
    PkgConfig::FLAC
    PkgConfig::ALSA
    pthread
)

if(FLAC_AX_USE_AX_SDK)
    target_link_libraries(flac_ax PRIVATE ax::core)
    target_compile_definitions(flac_ax PRIVATE FLAC_AX_HAS_AX_SDK=1)
endif()

# --- 2. Unified [AX] Audio/Visual Synth, Mixer & Spectrum Studio ---
if(FLAC_AX_BUILD_AV_STUDIO)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    find_package(OpenGL REQUIRED)

    add_executable(ax_av_studio
        src/studio_main.cpp
        src/midi/mpk_mini3.cpp
        src/dsp/avx2_synth.cpp
        src/visual/av_hud.cpp
        ${FLAC_AX_DSP_STUDIO_HEADERS}
    )

    target_include_directories(ax_av_studio PRIVATE src)

    target_compile_options(ax_av_studio PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>$<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_NATIVE_ARCH}>>:-march=native>
        -Wall -Wextra -Wpedantic
    )

    target_link_libraries(ax_av_studio PRIVATE
        PkgConfig::ALSA
        PkgConfig::FLAC
        PkgConfig::SDL2
        OpenGL::GL
        pthread
    )

    if(FLAC_AX_USE_AX_SDK)
        target_link_libraries(ax_av_studio PRIVATE ax::core)
        target_compile_definitions(ax_av_studio PRIVATE FLAC_AX_HAS_AX_SDK=1)
    endif()
endif()

if(FLAC_AX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET flac_ax PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(TARGET ax_av_studio)
            set_property(TARGET ax_av_studio PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
endif()
