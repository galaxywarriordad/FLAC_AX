cmake_minimum_required(VERSION 3.24)
project(flac_ax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_USE_AX_SDK "Fetch and link ax-sdk (v1_36)" OFF)
option(FLAC_AX_NATIVE_ARCH "Optimize for host CPU (-march=native) on bare-metal target" OFF)
option(FLAC_AX_ENABLE_LTO "Enable Link-Time Optimization (IPO/LTO) in Release builds" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_executable(flac_ax src/main.cpp)
target_include_directories(flac_ax PRIVATE src)

target_compile_options(flac_ax PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>$<$<AND:$<CONFIG:Release>,$<BOOL:${FLAC_AX_NATIVE_ARCH}>>:-march=native>
    -Wall -Wextra -Wpedantic
)

if(FLAC_AX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET flac_ax PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()

target_link_libraries(flac_ax PRIVATE
    PkgConfig::FLAC
    PkgConfig::ALSA
    pthread
)

if(FLAC_AX_USE_AX_SDK)
    include(FetchContent)
    FetchContent_Declare(
        ax_sdk
        GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
        GIT_TAG        v1_36_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ax_sdk)
    target_link_libraries(flac_ax PRIVATE ax::core)
    target_compile_definitions(flac_ax PRIVATE FLAC_AX_HAS_AX_SDK=1)
endif()
