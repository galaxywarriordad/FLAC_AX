cmake_minimum_required(VERSION 3.24)
project(flac_ax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_USE_AX_SDK "Fetch and link ax-sdk (v1_35_1c)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_executable(flac_ax src/main.cpp)
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
        GIT_TAG        v1_35_1c
        GIT_SHALLOW    FALSE
    )
    FetchContent_MakeAvailable(ax_sdk)
    target_link_libraries(flac_ax PRIVATE ax::core)
    target_compile_definitions(flac_ax PRIVATE FLAC_AX_HAS_AX_SDK=1)
endif()
