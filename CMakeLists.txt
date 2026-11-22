cmake_minimum_required(VERSION 3.24)
project(flac_ax LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLAC_AX_USE_AX_SDK "Fetch and link ax-sdk (v1_36_1)" ON)
option(FLAC_AX_BUILD_AV_STUDIO "Build ax_av_studio (MPK Mini MK3 + AVX2 Synth + A/V HUD)" ON)
option(FLAC_AX_BUILD_VIDEO_UPSCALER "Build single-stream 720p/1080p/2K DVD & V4L2 retro upscaler (ax_video_upscaler)" ON)
option(FLAC_AX_SKYLAKE_ARCH "Target Skylake microarchitecture (-march=skylake / /arch:AVX2)" ON)
option(FLAC_AX_ENABLE_LTO "Enable Link-Time Optimization (LLVM LTO / IPO) in Release builds" ON)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac++)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
endif()

if(FLAC_AX_USE_AX_SDK)
    if(NOT TARGET ax::core)
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
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(ax_sdk)
        endif()
    endif()
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

set(FLAC_AX_VIDEO_HEADERS
    src/video/ax_720p_hw_gate.hpp
    src/video/v4l2_live_capture.hpp
    src/video/upscale_720p_shader.hpp
    src/video/av_pts_sync.hpp
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

# --- 1. Headless Bit-Perfect ESS Sabre FLAC Player ---
add_executable(flac_ax
    src/main.cpp
    ${FLAC_AX_DSD_HEADERS}
)
flac_ax_apply_target_flags(flac_ax)
if(UNIX AND NOT APPLE)
    target_link_libraries(flac_ax PRIVATE PkgConfig::FLAC PkgConfig::ALSA)
endif()

# --- 2. Unified [AX] Audio/Visual Synth, Mixer & Spectrum Studio ---
if(FLAC_AX_BUILD_AV_STUDIO)
    if(UNIX AND NOT APPLE)
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
    endif()
endif()

# --- 3. Single-Stream 720p/1080p/2K DVD & V4L2 Retro Upscaler ---
if(FLAC_AX_BUILD_VIDEO_UPSCALER)
    add_executable(ax_video_upscaler
        src/video_upscaler_main.cpp
        ${FLAC_AX_VIDEO_HEADERS}
    )
    flac_ax_apply_target_flags(ax_video_upscaler)
    if(UNIX AND NOT APPLE)
        target_link_libraries(ax_video_upscaler PRIVATE PkgConfig::ALSA)
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
        if(TARGET ax_video_upscaler)
            set_property(TARGET ax_video_upscaler PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
endif()
```[cite: 12]

---

### 4. `FLAC_AX/src/main.cpp` (Full File — `ax::audio::LockFreeStereoRing` API Aligned)

```cpp
// src/main.cpp — Bare-metal Linux FLAC -> ALSA prototype with ESS DAC & RAM preload hooks
#include "ess_dac_hooks.hpp"
#include "preload_policy.hpp"

#include <FLAC++/decoder.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/audio.hpp>)
#include <ax/audio.hpp>
#define FLAC_AX_HAVE_AX_AUDIO_HPP 1
#endif
#endif

namespace {

std::atomic<bool> g_stop_requested{false};

void HandleSignal(int /*sig*/) noexcept {
    g_stop_requested.store(true, std::memory_order_release);
}

template <std::size_t CapacityFrames>
class StereoFrameRing {
public:
#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        if (interleaved_lr == nullptr || frames == 0) {
            return false;
        }
        return ax_ring_.Push(interleaved_lr, frames) == frames;
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        return ax_ring_.Pop(out_interleaved_lr, max_frames);
    }

    std::size_t Size() const noexcept {
        return ax_ring_.AvailableFrames();
    }
#else
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t used = (head + CapacityFrames - tail) % CapacityFrames;
        const std::size_t free_slots = (CapacityFrames - 1) - used;
        if (frames > free_slots) {
            return false;
        }

        const std::size_t first = std::min(frames, CapacityFrames - head);
        std::memcpy(&buffer_[head][0], interleaved_lr, first * 2 * sizeof(std::int32_t));
        const std::size_t second = frames - first;
        if (second > 0) {
            std::memcpy(&buffer_[0][0], interleaved_lr + first * 2, second * 2 * sizeof(std::int32_t));
        }

        head_.store((head + frames) % CapacityFrames, std::memory_order_release);
        return true;
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t available = (head + CapacityFrames - tail) % CapacityFrames;
        const std::size_t to_pop = std::min(max_frames, available);
        if (to_pop == 0) {
            return 0;
        }

        const std::size_t first = std::min(to_pop, CapacityFrames - tail);
        std::memcpy(out_interleaved_lr, &buffer_[tail][0], first * 2 * sizeof(std::int32_t));
        const std::size_t second = to_pop - first;
        if (second > 0) {
            std::memcpy(out_interleaved_lr + first * 2, &buffer_[0][0], second * 2 * sizeof(std::int32_t));
        }

        tail_.store((tail + to_pop) % CapacityFrames, std::memory_order_release);
        return to_pop;
    }

    std::size_t Size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head + CapacityFrames - tail) % CapacityFrames;
    }
#endif

private:
#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    ax::audio::LockFreeStereoRing<CapacityFrames, std::int32_t> ax_ring_{};
#else
    alignas(64) std::array<std::array<std::int32_t, 2>, CapacityFrames> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
#endif
};

struct StreamMeta {
    std::atomic<unsigned int> sample_rate{0};
    std::atomic<unsigned int> bits_per_sample{0};
    std::atomic<unsigned int> channels{0};
    std::atomic<bool> meta_ready{false};
    std::atomic<bool> eof_or_error{false};
};

inline std::int32_t SampleToS32(FLAC__int32 sample, unsigned int bps) noexcept {
    if (bps >= 32U) {
        return static_cast<std::int32_t>(sample);
    }
    if (bps == 0U) {
        return 0;
    }
    const unsigned int shift = 32U - bps;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(sample) << shift);
}

class FlacStreamDecoder final : public FLAC::Decoder::File {
public:
    FlacStreamDecoder(StereoFrameRing<65536>& ring, StreamMeta& meta)
        : ring_(ring), meta_(meta) {}

protected:
    void on_metadata(const ::FLAC__StreamMetadata* metadata) override {
        if (metadata->type == ::FLAC__METADATA_TYPE_STREAMINFO) {
            meta_.sample_rate.store(metadata->data.stream_info.sample_rate, std::memory_order_release);
            meta_.bits_per_sample.store(metadata->data.stream_info.bits_per_sample, std::memory_order_release);
            meta_.channels.store(metadata->data.stream_info.channels, std::memory_order_release);
            meta_.meta_ready.store(true, std::memory_order_release);
        }
    }

    ::FLAC__StreamDecoderWriteStatus write_callback(
        const ::FLAC__Frame* frame,
        const FLAC__int32* const buffer[]) override {
        const std::size_t blocksize = frame->header.blocksize;
        const unsigned int bps = frame->header.bits_per_sample;
        const unsigned int channels = frame->header.channels;

        if (pcm_scratch_.size() < blocksize * 2) {
            pcm_scratch_.resize(blocksize * 2);
        }

        for (std::size_t i = 0; i < blocksize; ++i) {
            const std::int32_t l = SampleToS32(buffer[0][i], bps);
            const std::int32_t r = (channels >= 2U) ? SampleToS32(buffer[1][i], bps) : l;
            pcm_scratch_[i * 2 + 0] = l;
            pcm_scratch_[i * 2 + 1] = r;
        }

        std::size_t offset = 0;
        while (offset < blocksize && !g_stop_requested.load(std::memory_order_acquire)) {
            if (ring_.Push(pcm_scratch_.data() + offset * 2, blocksize - offset)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        return g_stop_requested.load(std::memory_order_acquire)
                   ? FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
                   : FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void error_callback(::FLAC__StreamDecoderErrorStatus status) override {
        std::cerr << "[FLAC] Decoder error: " << ::FLAC__StreamDecoderErrorStatusString[status] << "\n";
        meta_.eof_or_error.store(true, std::memory_order_release);
    }

private:
    StereoFrameRing<65536>& ring_;
    StreamMeta& meta_;
    std::vector<std::int32_t> pcm_scratch_;
};

void ApplyThreadAffinityAndPriority(int cpu_id, int sched_fifo_prio) noexcept {
    if (cpu_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(cpu_id), &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
            std::cerr << "[WARN] Failed to set CPU affinity to core " << cpu_id << "\n";
        }
    }

    if (sched_fifo_prio > 0) {
        sched_param param{};
        param.sched_priority = sched_fifo_prio;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            std::cerr << "[WARN] Failed to set SCHED_FIFO priority " << sched_fifo_prio
                      << " (check CAP_SYS_NICE / ulimit)\n";
        }
    }
}

bool ConfigureAlsaForEss(snd_pcm_t* pcm,
                         const flac_ax::ess::EssChipProfile& chip,
                         unsigned int stream_rate_hz,
                         snd_pcm_uframes_t period_frames,
                         unsigned int& out_actual_rate,
                         snd_pcm_uframes_t& out_actual_period) {
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);

    int err = snd_pcm_hw_params_any(pcm, params);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_any failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_access failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_format(pcm, params, chip.preferred_format);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_format(S32_LE) failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_channels(pcm, params, 2);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_channels(2) failed: " << snd_strerror(err) << "\n";
        return false;
    }

    const unsigned int negotiated = flac_ax::ess::NegotiateEssRate(chip, stream_rate_hz);
    unsigned int rate = negotiated;
    int dir = 0;
    err = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_rate_near failed: " << snd_strerror(err) << "\n";
        return false;
    }

    snd_pcm_uframes_t period = period_frames;
    err = snd_pcm_hw_params_set_period_size_near(pcm, params, &period, &dir);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_period_size_near failed: " << snd_strerror(err) << "\n";
        return false;
    }

    snd_pcm_uframes_t buffer = period * 4;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_buffer_size_near failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_prepare(pcm);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_prepare failed: " << snd_strerror(err) << "\n";
        return false;
    }

    out_actual_rate = rate;
    out_actual_period = period;

    if (!flac_ax::ess::OnEssHardwareConfigured(chip, pcm, out_actual_rate, out_actual_period)) {
        std::cerr << "[ESS] Hardware state verification failed for " << chip.chip_name << "\n";
        return false;
    }

    std::cout << "[ESS] Chip=" << chip.chip_name
              << " rate=" << out_actual_rate << " Hz (stream=" << stream_rate_hz << " Hz)"
              << ", period=" << out_actual_period
              << " frames, buffer=" << buffer << " frames\n";
    return true;
}

std::filesystem::path ResolveHardwareProfilesPath(std::string_view cli_path) {
    if (!cli_path.empty()) {
        return std::filesystem::path(cli_path);
    }
    if (const char* env_dir = std::getenv("AX_SDK_DIR"); env_dir && *env_dir) {
        return std::filesystem::path(env_dir) / "config" / "hardware_profiles.json";
    }
    return std::filesystem::path("../ax-sdk/config/hardware_profiles.json");
}

void PrintUsage() {
    std::cerr << "Usage: flac_ax <file.flac> [options]\n"
              << "  --ess <es9281ac_pro|es9281pro>   Target ESS DAC chip hook (default: es9281ac_pro)\n"
              << "  --device <alsa_dev>              ALSA PCM device (default: chip default)\n"
              << "  --period <frames>                ALSA period frames (default: chip default 96)\n"
              << "  --profiles <path/to/json>        Cross-check ax-sdk hardware_profiles.json\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string flac_path;
    std::string ess_arg = "es9281ac_pro";
    std::string device_override;
    std::string profiles_arg;
    bool has_period_override = false;
    snd_pcm_uframes_t period_frames = 96U;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--ess" && i + 1 < argc) {
            ess_arg = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            device_override = argv[++i];
        } else if (arg == "--period" && i + 1 < argc) {
            period_frames = static_cast<snd_pcm_uframes_t>(std::stoul(argv[++i]));
            has_period_override = true;
        } else if (arg == "--profiles" && i + 1 < argc) {
            profiles_arg = argv[++i];
        } else if (!arg.empty() && arg.front() != '-' && flac_path.empty()) {
            flac_path = std::string(arg);
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (flac_path.empty()) {
        PrintUsage();
        return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const auto resolved_ess = flac_ax::ess::ResolveEssChipHook(ess_arg);
    if (!resolved_ess.has_value()) {
        std::cerr << "[ESS] Unrecognized ESS chip spec '" << ess_arg
                  << "'. Use 'es9281ac_pro' or 'es9281pro'.\n";
        return 1;
    }
    const flac_ax::ess::EssChipProfile chip = *resolved_ess;
    const std::string device =
        !device_override.empty() ? device_override : std::string(chip.default_alsa_device);
    if (!has_period_override) {
        period_frames = chip.default_period_frames;
    }

    const auto profiles_path = ResolveHardwareProfilesPath(profiles_arg);
    const auto matrix = flac_ax::ess::CrossCheckHardwareProfiles(profiles_path);
    if (matrix.file_found) {
        std::cout << "[AX-SDK] Cross-checked " << profiles_path.string()
                  << " (v2.1=" << (matrix.has_v21_schema ? "yes" : "no")
                  << ", output_device=auto=" << (matrix.common_audio_device_auto ? "yes" : "no")
                  << ", cpu_match=" << (matrix.matched_cpu_profile.empty() ? "none" : matrix.matched_cpu_profile)
                  << ")\n";
    }

    const auto preload_policy = flac_ax::DetectHostPreloadPolicy();
    std::cout << "[PRELOAD] MemTotal=" << (preload_policy.detected_mem_total_bytes / (1024ULL * 1024ULL))
              << " MiB, mode="
              << (preload_policy.mode == flac_ax::PreloadMode::WholeAlbum ? "WholeAlbum" :
                  (preload_policy.mode == flac_ax::PreloadMode::Window3Files ? "Window3Files" : "SingleFile"))
              << "\n";

    StereoFrameRing<65536> ring;
    StreamMeta meta;
    std::atomic<bool> decode_finished{false};

    std::thread decoder_thread([&]() {
        ApplyThreadAffinityAndPriority(5, 0);

        FlacStreamDecoder decoder(ring, meta);
        const FLAC__StreamDecoderInitStatus init_status = decoder.init(flac_path);
        if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            std::cerr << "[FLAC] Failed to initialize decoder for '" << flac_path << "' (status="
                      << FLAC__StreamDecoderInitStatusString[init_status] << ")\n";
            meta.eof_or_error.store(true, std::memory_order_release);
            decode_finished.store(true, std::memory_order_release);
            return;
        }

        decoder.process_until_end_of_stream();
        decoder.finish();
        decode_finished.store(true, std::memory_order_release);
    });

    while (!meta.meta_ready.load(std::memory_order_acquire) &&
           !meta.eof_or_error.load(std::memory_order_acquire) &&
           !g_stop_requested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (meta.eof_or_error.load(std::memory_order_acquire) ||
        g_stop_requested.load(std::memory_order_acquire)) {
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    const unsigned int stream_rate = meta.sample_rate.load(std::memory_order_acquire);
    const unsigned int bps = meta.bits_per_sample.load(std::memory_order_acquire);
    const unsigned int channels = meta.channels.load(std::memory_order_acquire);
    std::cout << "[FLAC] Stream: " << stream_rate << " Hz, " << bps << "-bit, " << channels << " ch\n";

    snd_pcm_t* pcm = nullptr;
    int open_err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (open_err < 0) {
        std::cerr << "[ALSA] Failed to open '" << device << "': " << snd_strerror(open_err) << "\n";
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    unsigned int actual_rate = 0;
    snd_pcm_uframes_t actual_period = 0;
    if (!ConfigureAlsaForEss(pcm, chip, stream_rate, period_frames, actual_rate, actual_period)) {
        snd_pcm_close(pcm);
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    ApplyThreadAffinityAndPriority(0, 80);

    std::vector<std::int32_t> out_buffer(actual_period * 2, 0);
    std::uint64_t xruns = 0;
    std::uint64_t total_frames_written = 0;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        const std::size_t avail = ring.Size();
        if (avail == 0) {
            if (decode_finished.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
            continue;
        }

        const std::size_t to_write = std::min<std::size_t>(avail, static_cast<std::size_t>(actual_period));
        const std::size_t popped = ring.Pop(out_buffer.data(), to_write);
        if (popped == 0) {
            continue;
        }

        std::size_t frame_offset = 0;
        while (frame_offset < popped && !g_stop_requested.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, out_buffer.data() + frame_offset * 2, popped - frame_offset);
            if (written >= 0) {
                frame_offset += static_cast<std::size_t>(written);
                total_frames_written += static_cast<std::size_t>(written);
            } else if (written == -EAGAIN) {
                snd_pcm_wait(pcm, 10);
            } else if (written == -EPIPE) {
                ++xruns;
                const int rec_err = snd_pcm_recover(pcm, static_cast<int>(written), 1);
                if (rec_err < 0) {
                    std::cerr << "[ALSA] Failed to recover from EPIPE: " << snd_strerror(rec_err) << "\n";
                    g_stop_requested.store(true, std::memory_order_release);
                    break;
                }
            } else {
                std::cerr << "[ALSA] snd_pcm_writei error: " << snd_strerror(static_cast<int>(written)) << "\n";
                g_stop_requested.store(true, std::memory_order_release);
                break;
            }
        }
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);

    g_stop_requested.store(true, std::memory_order_release);
    decoder_thread.join();

    std::cout << "[DONE] Chip=" << chip.chip_name
              << ", frames=" << total_frames_written
              << ", rate=" << actual_rate << " Hz, xruns=" << xruns << "\n";
    return 0;
}
```[cite: 20]

---

### 5. Local Desktop Files (`Eden-Espresso-Wave-AX` — Full Files)

#### `CMakeSettings.json`

```json
{
  "configurations": [
    {
      "name": "x64-LLVM-Skylake-Release",
      "generator": "Ninja",
      "configurationType": "Release",
      "buildRoot": "C:/Users/nick_/Desktop/eden/build_stage0",
      "installRoot": "${projectDir}/out/install/${name}",
      "cmakeExecutable": "C:/Program Files/CMake/bin/cmake.exe",
      "cmakeCommandArgs": "",
      "buildCommandArgs": "-j 4",
      "ctestCommandArgs": "",
      "inheritEnvironments": [ "clang_cl_x64_x64" ],
      "variables": [
        {
          "name": "CMAKE_C_COMPILER",
          "value": "C:/Program Files/LLVM/bin/clang-cl.exe",
          "type": "FILEPATH"
        },
        {
          "name": "CMAKE_CXX_COMPILER",
          "value": "C:/Program Files/LLVM/bin/clang-cl.exe",
          "type": "FILEPATH"
        },
        {
          "name": "CMAKE_LINKER",
          "value": "C:/Program Files/LLVM/bin/lld-link.exe",
          "type": "FILEPATH"
        },
        {
          "name": "ENABLE_LSFG",
          "value": "ON",
          "type": "BOOL"
        }
      ]
    }
  ]
}
```[cite: 8]

#### `CMakeLists.txt`

```cmake
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "macOS deployment target")
project(yuzu)

# =======================================================================
# HARDENED WINDOWS 10 ENVIRONMENT SUBSYSTEM TARGET FLUSH
# =======================================================================
if (WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 _HAS_CXX17=1)
    message(STATUS "[ENV CONTROLLER] Injected absolute Windows 10 socket environment variables across all targets.")
endif()
# =======================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/find")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/externals/cmake-modules")

set(CPM_SOURCE_CACHE ${CMAKE_SOURCE_DIR}/.cache/cpm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES 0)

include(DetectPlatform)
include(DetectArchitecture)
include(DefaultConfig)
include(UseLTO)
include(FasterLinker)
include(UseCcache)
include(CMakeDependentOption)
include(CTest)
include(CPMUtil)

if (NOT DEFINED ARCHITECTURE)
    message(FATAL_ERROR "Architecture didn't make it out of scope, did you delete DetectArchitecture.cmake?")
endif()

if (OPENBSD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_SYSROOT}/usr/X11R6/include -D_LIBCPP_PSTL_BACKEND_SERIAL=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${CMAKE_SYSROOT}/usr/X11R6/include -D_LIBCPP_PSTL_BACKEND_SERIAL=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_SYSROOT}/usr/X11R6/lib")
elseif (NETBSD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_SYSROOT}/usr/X11R7/include -I${CMAKE_SYSROOT}/usr/pkg/include/c++/v1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${CMAKE_SYSROOT}/usr/X11R7/include -I${CMAKE_SYSROOT}/usr/pkg/include/c++/v1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_SYSROOT}/usr/X11R7/lib")
endif()

if (NETBSD)
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}:${CMAKE_SYSROOT}/usr/pkg/lib/ffmpeg7/pkgconfig")
endif()

cmake_dependent_option(YUZU_STATIC_ROOM "Build a static room executable only (CI only)" OFF "LINUX" OFF)
if (YUZU_STATIC_ROOM)
    set(YUZU_ROOM ON)
    set(YUZU_ROOM_STANDALONE ON)
    set(ENABLE_QT OFF)
    set(YUZU_CMD OFF)
    set(ENABLE_CUBEB OFF)
    set(ENABLE_UPDATE_CHECKER OFF)
    set(USE_DISCORD_PRESENCE OFF)
    set(BUILD_TESTING OFF)
    set(ENABLE_WEB_SERVICE OFF)
    set(ENABLE_LIBUSB OFF)
    set(Boost_USE_STATIC_LIBS ON)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(OPENSSL_USE_STATIC_LIBS ON)
    set(OpenSSL_FORCE_SYSTEM ON)
    set(zstd_FORCE_BUNDLED ON)
    set(fmt_FORCE_BUNDLED ON)
endif()

option(ENABLE_QT "Enable the Qt frontend" ON)
option(ENABLE_QT_TRANSLATION "Enable translations for the Qt frontend" OFF)
option(ENABLE_UPDATE_CHECKER "Enable update checker (for Qt)" OFF)
cmake_dependent_option(YUZU_USE_QT_MULTIMEDIA "Use QtMultimedia for Camera"  OFF "NOT YUZU_USE_BUNDLED_QT" OFF)
cmake_dependent_option(YUZU_USE_QT_WEB_ENGINE "Use QtWebEngine for web applet implementation" OFF "NOT YUZU_USE_BUNDLED_QT" OFF)
set(YUZU_QT_MIRROR "" CACHE STRING "What mirror to use for downloading the bundled Qt libraries")
cmake_dependent_option(YUZU_USE_BUNDLED_QT "Download bundled Qt binaries" "${MSVC}" "ENABLE_QT" OFF)

option(ENABLE_DEBUG_TOOLS "Enable debugging tools (maxwell disassembler, SPIRV translator, etc)" OFF)
option(ENABLE_WERROR "Enable -Werror diagnostics" ON)

option(ENABLE_LSFG "Enable Lossless Scaling frame generation" ON)
option(ENABLE_RESHADE "Enable ReShade FX post-processing effects" ON)

if (YUZU_USE_BUNDLED_QT AND (APPLE OR NOT UNIX))
    set(YUZU_STATIC_BUILD ON)
endif()

option(YUZU_STATIC_BUILD "Use static libraries and executables if available" OFF)
if (YUZU_STATIC_BUILD)
    set(Boost_USE_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(CMAKE_IMPORT_LIBRARY_SUFFIX ".a")

    if (MINGW)
        function(find_library var)
            if (${var} AND NOT ${var} MATCHES "dll\\.a$")
                return()
            endif()
            _find_library(${var} ${ARGN})
            if (${var})
                get_filename_component(lib_name "${${var}}" NAME)
                if (lib_name MATCHES "dll\\.a$")
                    unset(${var} CACHE)
                    set(${var} "${var}-NOTFOUND" CACHE INTERNAL "" FORCE)
                endif()
            endif()
        endfunction()
        set(QuaZip-Qt6_FORCE_BUNDLED ON)
        set(YUZU_USE_BUNDLED_FFMPEG ON)
        set(YUZU_USE_BUNDLED_SDL3 ON)
        set(YUZU_USE_BUNDLED_OPENSSL ON)
        set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF)
    elseif(APPLE)
        set(YUZU_USE_BUNDLED_FFMPEG ON)
        set(YUZU_USE_BUNDLED_SDL3 ON)
        set(YUZU_USE_BUNDLED_OPENSSL ON)
        set(fmt_FORCE_BUNDLED ON)
        set(SPIRV-Headers_FORCE_BUNDLED ON)
        set(zstd_FORCE_BUNDLED ON)
    endif()
endif()

if (MSVC AND ARCHITECTURE_x86)
    message(FATAL_ERROR "Attempting to build with the x86 environment is not supported.")
endif()

if (CXX_CLANG_CL)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-command-line-argument>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unsafe-buffer-usage>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-value>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-extra-semi-stmt>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-sign-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-reserved-identifier>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-deprecated-declarations>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-cast-function-type-mismatch>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c99-extensions>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++17-compat>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++11-compat-reserved-user-defined-literal>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++11-compat-deprecated-writable-strings>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++11-compat-pedantic>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++11-compat>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++0x-compat>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++98-c++11-compat-binary-literal>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++98-compat-pedantic>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c++98-compat>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-c99-compat>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=all>
        $<$<COMPILE_LANGUAGE:C,CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:C,CXX>:/fp:precise>)

    if (ARCHITECTURE_x86_64)
        add_compile_options(
            $<$<COMPILE_LANGUAGE:C,CXX>:-march=skylake>
            $<$<COMPILE_LANGUAGE:C,CXX>:-msse4.1>
            $<$<COMPILE_LANGUAGE:C,CXX>:-msse4.2>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mavx>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mavx2>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mfma>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mbmi>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mbmi2>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mpopcnt>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mlzcnt>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mf16c>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mrdrnd>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mfsgsbase>
            $<$<COMPILE_LANGUAGE:C,CXX>:-mcx16>
        )
        message(STATUS "[ARCH ENGINE] Showcase LLVM Skylake/Coffee Lake ISA matrix locked.")
    endif()
endif()

if (MSVC AND NOT CXX_CLANG)
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} /W3 /WX-")
endif()

if(MSVC)
    if (YUZU_USE_BUNDLED_QT AND ARCHITECTURE_arm64)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(libflag MT)
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set(libflag MD)
    endif()

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /${libflag}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /${libflag}d")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /${libflag}")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /${libflag}d")

    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:Debug>:/${libflag}d>>
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:Release>:/${libflag}>>
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:RelWithDebInfo>:/${libflag}>>
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:MinSizeRel>:/${libflag}>>)

    # =======================================================================
    # INJECTION POINT 1: Global Linker Isolation Defenses for Standalone PGO
    # =======================================================================
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:libcmt.lib /ALLOWISOLATION"
    )
    message(STATUS "[GLOBAL LINKER OVERRIDE] Enforced release runtime library isolation limits successfully.")
endif()

option(ENABLE_CUBEB "Enables the cubeb audio backend" ON)

set(EXT_DEFAULT OFF)
if (MSVC)
    set(EXT_DEFAULT ON)
endif()

option(YUZU_USE_BUNDLED_SDL3 "Download bundled SDL3 build" ${EXT_DEFAULT})
option(YUZU_USE_BUNDLED_FFMPEG "Download bundled FFmpeg" ${EXT_DEFAULT})
cmake_dependent_option(YUZU_USE_EXTERNAL_FFMPEG "Build FFmpeg from external source" "${SOLARIS}" "NOT WIN32" OFF)

set(BUNDLED_SIRIT_DEFAULT OFF)
if (MSVC AND NOT (CMAKE_BUILD_TYPE MATCHES "Deb"))
    set(BUNDLED_SIRIT_DEFAULT ON)
endif()

option(YUZU_USE_BUNDLED_SIRIT "Download bundled sirit" ${BUNDLED_SIRIT_DEFAULT})
cmake_dependent_option(ENABLE_LIBUSB "Enable the use of LibUSB" ON "WIN32 OR LINUX OR FREEBSD OR APPLE" OFF)
cmake_dependent_option(ENABLE_OPENGL "Enable OpenGL" ON "NOT (WIN32 AND ARCHITECTURE_arm64) AND NOT APPLE" OFF)
mark_as_advanced(FORCE ENABLE_OPENGL)

option(ENABLE_WEB_SERVICE "Enable web services (telemetry, etc.)" ON)
option(ENABLE_WIFI_SCAN "Enable WiFi scanning" OFF)
cmake_dependent_option(USE_DISCORD_PRESENCE "Enables Discord Rich Presence" OFF "ENABLE_QT" OFF)
option(YUZU_TESTS "Compile tests" "${BUILD_TESTING}")
cmake_dependent_option(YUZU_INSTALL_UDEV_RULES "Install udev rules for gyro access" OFF "LINUX" OFF)
option(YUZU_LEGACY "Apply patches that improve compatibility with older GPUs" OFF)
option(NIGHTLY_BUILD "Use Nightly qualifiers in the update checker and build metadata" OFF)
cmake_dependent_option(YUZU_ROOM "Enable dedicated room functionality" ON "LINUX OR WIN32 OR APPLE OR FREEBSD OR OPENBSD OR NETBSD" OFF)
cmake_dependent_option(YUZU_ROOM_STANDALONE "Enable standalone room executable" ON "YUZU_ROOM" OFF)
cmake_dependent_option(YUZU_CMD "Compile the eden-cli executable" ON "LINUX OR WIN32 OR APPLE OR FREEBSD OR OPENBSD OR NETBSD" OFF)
cmake_dependent_option(YUZU_CRASH_DUMPS "Compile crash dump (Minidump) support" OFF "WIN32 OR LINUX" OFF)
option(YUZU_DOWNLOAD_TIME_ZONE_DATA "Always download time zone binaries" ON)
set(YUZU_TZDB_PATH "" CACHE STRING "Path to a pre-downloaded timezone database")

cmake_dependent_option(YUZU_USE_BUNDLED_MOLTENVK "Download bundled MoltenVK lib" ON "APPLE" OFF)

option(YUZU_DISABLE_LLVM "Disable LLVM (useful for CI)" OFF)

if (WIN32)
    set(LLVM_DIR "C:/Program Files/LLVM" CACHE PATH "Path to standalone LLVM installation")
    message(STATUS "[LLVM FACTORY] Standard link maps bound to host path: ${LLVM_DIR}")
endif()

set(DEFAULT_YUZU_USE_BUNDLED_OPENSSL OFF)
if (EXT_DEFAULT OR SOLARIS OR OPENBSD)
    set(DEFAULT_YUZU_USE_BUNDLED_OPENSSL ON)
endif()

option(YUZU_USE_BUNDLED_OPENSSL "Download bundled OpenSSL build" ${DEFAULT_YUZU_USE_BUNDLED_OPENSSL})

if(EXISTS ${PROJECT_SOURCE_DIR}/hooks/pre-commit AND NOT EXISTS ${PROJECT_SOURCE_DIR}/.git/hooks/pre-commit)
    if (EXISTS ${PROJECT_SOURCE_DIR}/.git/)
        message(STATUS "Copying pre-commit hook")
        file(COPY hooks/pre-commit DESTINATION ${PROJECT_SOURCE_DIR}/.git/hooks)
    endif()
endif()

if (ARCHITECTURE_arm64 AND LINUX)
    set(HAS_NCE 1)
    add_compile_definitions(HAS_NCE=1)
endif()

if (YUZU_ROOM)
    add_compile_definitions(YUZU_ROOM)
endif()

if (UNIX AND NOT (LINUX OR WIN32))
    if(CXX_APPLE OR CXX_CLANG)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fexperimental-library")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LIBCPP_ENABLE_EXPERIMENTAL=1")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LIBCPP_ENABLE_EXPERIMENTAL=1")
    endif()
endif()

if (CXX_GCC OR CXX_CLANG)
    if (ARCHITECTURE_x86_64)
        set(YUZU_BUILD_PRESET "skylake" CACHE STRING "Build preset to use.")
        if (${YUZU_BUILD_PRESET} STREQUAL "generic")
            set(march x86-64)
            set(mtune generic)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "skylake")
            set(march skylake)
            set(mtune skylake)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "v3")
            set(march x86-64-v3)
            set(mtune generic)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "zen2")
            set(march znver2)
            set(mtune znver2)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "zen4")
            set(march znver4)
            set(mtune znver4)
        endif()
    elseif(ARCHITECTURE_arm64)
        set(YUZU_BUILD_PRESET "custom" CACHE STRING "Build preset to use.")
        set(mtune generic)
        if (${YUZU_BUILD_PRESET} STREQUAL "generic")
            set(march armv8-a)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "optimized")
            set(march armv8.2-a+lse+rcpc)
        elseif (${YUZU_BUILD_PRESET} STREQUAL "armv9")
            set(march armv9-a)
        endif()
    endif()

    if (DEFINED march AND DEFINED mtune)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${march} -mtune=${mtune}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=${march} -mtune=${mtune}")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(RenderDoc MODULE)

if (YUZU_USE_BUNDLED_OPENSSL)
    set(BUILD_SHARED_LIBS OFF)
    AddJsonPackage(openssl-ci)
else()
    AddJsonPackage(openssl)
    set(OPENSSL_BUILD_VERBOSE ON)
    set(OPENSSL_CONFIGURE_VERBOSE ON)
    if (OpenSSL_ADDED)
        AddJsonPackage(openssl-cmake)
    endif()
endif()

if (OpenSSL_ADDED)
    add_compile_definitions(YUZU_BUNDLED_OPENSSL)
endif()

message(STATUS "Fetching needed dependencies with CPM")
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(ENABLE_TESTING OFF)

set(BOOST_INCLUDE_LIBRARIES algorithm icl pool container heap asio headers process filesystem crc variant)
AddJsonPackage(boost)
set(BOOST_NO_HEADERS ${Boost_ADDED})

if (Boost_ADDED)
    add_compile_definitions(YUZU_BOOST_v1)
    if (NOT MSVC OR CXX_CLANG)
        if (SOLARIS)
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-pthreads>)
        endif()
        target_compile_options(boost_heap INTERFACE $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-shadow>)
        target_compile_options(boost_icl INTERFACE $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-shadow>)
        target_compile_options(boost_asio INTERFACE
            $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
            $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-implicit-fallthrough>)
    endif()
endif()

AddJsonPackage(fmt)
AddJsonPackage(lz4)

if (lz4_ADDED)
    add_library(lz4::lz4 ALIAS lz4_static)
endif()

AddJsonPackage(zstd)

if (zstd_ADDED)
    add_library(zstd::zstd ALIAS libzstd_static)
    add_library(zstd::libzstd ALIAS libzstd_static)
endif()

AddJsonPackage(nlohmann)

# =======================================================================
# AX-SDK (Shared C++20 Silicon Core, Settings, Telemetry & Tools)
# =======================================================================
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_AX_SDK)
    if(DEFINED ENV{AX_SDK_DIR} AND EXISTS "$ENV{AX_SDK_DIR}/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_AX_SDK "$ENV{AX_SDK_DIR}" CACHE PATH "" FORCE)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_AX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk" CACHE PATH "" FORCE)
    endif()
endif()

FetchContent_Declare(
    ax_sdk
    GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
    GIT_TAG        v1_36_1
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(ax_sdk)

if (NOT YUZU_STATIC_ROOM)
    AddJsonPackage(zlib)
    if (ZLIB_ADDED)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()

    AddJsonPackage(opus)
    if (Opus_ADDED)
        if (MSVC AND CXX_CLANG)
            target_compile_options(opus PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-implicit-function-declaration>)
        endif()
    endif()

    if (NOT TARGET Opus::opus)
        add_library(Opus::opus ALIAS opus)
    endif()
endif()

if(NOT TARGET Boost::headers)
    AddJsonPackage(boost_headers)
endif()

set(FFmpeg_COMPONENTS avcodec avfilter avutil swscale)

function(create_target_directory_groups target_name)
    source_group("Other Files" REGULAR_EXPRESSION ".")
    get_target_property(target_sources "${target_name}" SOURCES)
    foreach(file_name IN LISTS target_sources)
        get_filename_component(dir_name "${file_name}" PATH)
        string(REPLACE "/" "\\" group_name "${dir_name}")
        source_group("${group_name}" FILES "${file_name}")
    endforeach()
endfunction()

if (APPLE)
    foreach(fw Carbon Metal Cocoa IOKit CoreVideo CoreMedia Security UniformTypeIdentifiers Foundation)
        find_library(${fw}_LIBRARY ${fw} REQUIRED)
        list(APPEND PLATFORM_LIBRARIES ${${fw}_LIBRARY})
    endforeach()
elseif (WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)

    set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} winmm iphlpapi ws2_32 wlanapi)

    if (MINGW)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} psapi imm32 version crypt32 rpcrt4 gdi32 wldap32 mswsock)
    endif()
elseif (MANAGARM)
    set(PLATFORM_LIBRARIES iconv intl)
elseif (HAIKUOS)
    set(PLATFORM_LIBRARIES bsd /boot/system/lib/libnetwork.so)
elseif (CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU|SunOS)$")
    set(PLATFORM_LIBRARIES rt)
endif()

message(STATUS "Platform Libraries: ${PLATFORM_LIBRARIES}")
add_subdirectory(externals)
find_package(enet)

if (ARCHITECTURE_x86 OR ARCHITECTURE_x86_64)
    find_package(xbyak)
endif()

if (NOT YUZU_STATIC_ROOM)
    find_package(libusb)
    find_package(VulkanMemoryAllocator)
    find_package(VulkanUtilityLibraries)
    find_package(SimpleIni)
    find_package(sirit)
    find_package(gamemode)
    find_package(frozen)

    if (ARCHITECTURE_riscv64)
        find_package(biscuit)
    endif()

    if (ENABLE_WEB_SERVICE OR ENABLE_UPDATE_CHECKER)
        find_package(cpp-jwt)
    endif()

    if (ARCHITECTURE_arm64 OR DYNARMIC_TESTS)
        find_package(oaknut)
    endif()

    find_package(SDL3)

    if (USE_DISCORD_PRESENCE)
        find_package(DiscordRPC)
    endif()

    if (ENABLE_CUBEB)
        find_package(cubeb)
    endif()

    if (YUZU_TESTS OR DYNARMIC_TESTS)
        find_package(Catch2)
    endif()
endif()

if (ENABLE_QT)
    if (YUZU_USE_BUNDLED_QT)
        AddQt(Eden-CI/Qt 6.11.1)
    else()
        message(STATUS "Using system Qt")
        if (NOT Qt6_DIR)
            set(Qt6_DIR "" CACHE PATH "Additional path to search for Qt6 libraries")
        endif()
        list(APPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
    endif()

    find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

    if (NOT DEFINED QT_TARGET_PATH)
        get_target_property(qtcore_path Qt6::Core LOCATION_Release)
        string(FIND "${qtcore_path}" "/bin/" qtcore_path_bin_pos REVERSE)
        string(FIND "${qtcore_path}" "/lib/" qtcore_path_lib_pos REVERSE)
        if (qtcore_path_bin_pos GREATER qtcore_path_lib_pos)
            string(SUBSTRING "${qtcore_path}" 0 ${qtcore_path_bin_pos} QT_TARGET_PATH)
        else()
            string(SUBSTRING "${qtcore_path}" 0 ${qtcore_path_lib_pos} QT_TARGET_PATH)
        endif()
    endif()

    if (NOT DEFINED QT_HOST_PATH)
        set(QT_HOST_PATH "${QT_TARGET_PATH}")
    endif()

    set(YUZU_QT_COMPONENTS Core Widgets Charts Concurrent Gui)
    if (LINUX OR FREEBSD)
        list(APPEND YUZU_QT_COMPONENTS DBus)
        set(QT_NO_PRIVATE_MODULE_WARNING ON)
        list(APPEND YUZU_QT_OPTIONAL GuiPrivate)
    endif()

    if (YUZU_USE_QT_MULTIMEDIA)
        list(APPEND YUZU_QT_COMPONENTS Multimedia)
    endif()

    if (YUZU_USE_QT_WEB_ENGINE)
        list(APPEND YUZU_QT_COMPONENTS WebEngineCore WebEngineWidgets)
    endif()

    if (ENABLE_QT_TRANSLATION)
        list(APPEND YUZU_QT_COMPONENTS LinguistTools)
    endif()

    find_package(Qt6 REQUIRED COMPONENTS ${YUZU_QT_COMPONENTS} OPTIONAL_COMPONENTS ${YUZU_QT_OPTIONAL})
    set(QT_MAJOR_VERSION 6)
    set_target_properties(Qt6::Platform PROPERTIES INTERFACE_COMPILE_FEATURES "")
    AddJsonPackage(quazip)
endif()

if (NOT YUZU_STATIC_ROOM AND NOT (YUZU_USE_BUNDLED_FFMPEG OR YUZU_USE_EXTERNAL_FFMPEG))
    find_package(FFmpeg REQUIRED QUIET COMPONENTS ${FFmpeg_COMPONENTS})
    set_property(GLOBAL APPEND PROPERTY CPM_PACKAGE_NAMES FFmpeg)
    set_property(GLOBAL APPEND PROPERTY CPM_PACKAGE_SHAS "unknown (system)")
    set_property(GLOBAL APPEND PROPERTY CPM_PACKAGE_URLS "https://github.com")
endif()

if (MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(
        /wd4464
        /wd4711
        /wd4820
    )
endif()

if (MSVC AND CXX_CLANG)
    if (ARCHITECTURE_x86_64)
        set(FILE_ARCH x86_64)
    elseif (ARCHITECTURE_arm64)
        set(FILE_ARCH aarch64)
    else()
        message(FATAL_ERROR "clang-cl: Unsupported architecture ${ARCHITECTURE}")
    endif()

    AddJsonPackage(llvm-mingw)
    set(LIB_PATH "${llvm-mingw_SOURCE_DIR}/libclang_rt.builtins-${FILE_ARCH}.a")
    add_library(llvm-mingw-runtime STATIC IMPORTED)
    set_target_properties(llvm-mingw-runtime PROPERTIES IMPORTED_LOCATION "${LIB_PATH}")
    link_libraries(llvm-mingw-runtime)
endif()

# =======================================================================
# INJECTION POINT 3: MSVC STL COMPATIBILITY & AUDIO VECTORIZATION LOCK
# =======================================================================
if (MSVC AND CXX_CLANG)
    add_compile_options("/D_DISABLE_VECTOR_ANNOTATION")
    add_compile_definitions(_DISABLE_VECTOR_ANNOTATION)
    add_compile_options("/bigobj")
    message(STATUS "[GLOBAL STL FIX] Hard-enforced MSVC find-vectorizer bypass across all engine targets.")

    set(OPUS_TARGET_VARIATIONS "opus" "externals::opus" "bundled_libopus" "audio_core")
    foreach(_opus_target IN LISTS OPUS_TARGET_VARIATIONS)
        if(TARGET ${_opus_target})
            get_target_property(_current_flags ${_opus_target} COMPILE_OPTIONS)
            if(_current_flags)
                list(REMOVE_ITEM _current_flags "/fp:fast")
            else()
                set(_current_flags "")
            endif()
            list(APPEND _current_flags "/fp:precise" "-mavx2" "-mfma" "-mbmi2")
            set_target_properties(${_opus_target} PROPERTIES COMPILE_OPTIONS "${_current_flags}")
            message(STATUS "[OPUS OPTIMIZATION] Isolated /fp:precise, AVX2, and FMA for target: ${_opus_target}")
        endif()
    endforeach()
endif()
# =======================================================================

add_subdirectory(src)

if (TARGET core AND TARGET ax::core)
    target_link_libraries(core PRIVATE ax::core)
    message(STATUS "[AX-SDK] Linked ax::core (v1_36_1) into core engine target.")
endif()

if (ENABLE_DEBUG_TOOLS)
    add_subdirectory(tools/maxwell-disas)
    add_subdirectory(tools/maxwell-spirv)
    add_subdirectory(tools/maxwell-ir)
endif()

if(ENABLE_QT)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT yuzu)
else()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT yuzu-cmd)
endif()

# =======================================================================
# INJECTION POINT 2: Target-Specific Whole Archive Enforcement for Stage 1
# =======================================================================
if (MSVC AND CXX_CLANG)
    if (CMAKE_C_FLAGS_RELEASE MATCHES "fprofile-instr-generate" OR CMAKE_CXX_FLAGS_RELEASE MATCHES "fprofile-instr-generate")
        if (TARGET core)
            set_target_properties(core PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:clang_rt.profile-x86_64.lib")
            message(STATUS "[STAGE 1 LINK] Successfully bound whole-archive profiling hooks to the core engine target.")
        endif()
    endif()
endif()

if(ENABLE_QT AND UNIX AND NOT APPLE)
    install(FILES "dist/dev.eden_emu.eden.desktop" DESTINATION "share/applications")
    install(FILES "dist/dev.eden_emu.eden.svg" DESTINATION "share/icons/hicolor/scalable/apps")
    install(FILES "dist/dev.eden_emu.eden.xml" DESTINATION "share/mime/packages")
    install(FILES "dist/dev.eden_emu.eden.metainfo.xml" DESTINATION "share/metainfo")
endif()

if (YUZU_INSTALL_UDEV_RULES)
    include(GNUInstallDirs)
    install(FILES "dist/72-eden-input.rules" DESTINATION "${CMAKE_INSTALL_LIBDIR}/udev/rules.d")
endif()

# =======================================================================
# CLEAN PURE C LANGUAGE MATRIX FROM OVERLOADED C++ EXCLUSIVE FLAGS
# =======================================================================
if(NOT MSVC_IDE)
    get_property(CURRENT_C_FLAGS GLOBAL PROPERTY CMAKE_C_FLAGS_RELEASE)
    if(CURRENT_C_FLAGS)
        if(NOT CURRENT_C_FLAGS MATCHES "fprofile-instr")
            string(REGEX REPLACE "/std:c\\+\\+[0-9a-zA-Z]+" "" CLEANED_C_FLAGS "${CURRENT_C_FLAGS}")
            string(REGEX REPLACE "/Ob[1-3]" "" CLEANED_C_FLAGS "${CLEANED_C_FLAGS}")
            string(REGEX REPLACE "-clang:-Wlifetime" "" CLEANED_C_FLAGS "${CLEANED_C_FLAGS}")
            set(CMAKE_C_FLAGS_RELEASE "${CLEANED_C_FLAGS} /MD /O3 /Oi /Ot /Gw /Gy /DNDEBUG /D_WIN32_WINNT=0x0A00 /fp:precise" CACHE STRING "" FORCE)
        endif()
    endif()
endif()
```[cite: 9]

#### `Scripts/PGO_stage0I.bat`

```batch
@echo off
setlocal EnableDelayedExpansion

:: 1. Initialize Visual Studio x64 Native Tools & LLVM 23+ Toolchain Environment
set "PATH=C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;%PATH%"
if not defined VSCMD_VER (
    for %%V in (18 2026 2022) do (
        for %%E in (Community Professional Enterprise BuildTools) do (
            if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
                call "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
                goto :vs_ready
            )
        )
    )
)
:vs_ready

echo =======================================================================
echo  [STAGE 0I - INCREMENTAL] Fast Ninja Delta Compile ^& Link
echo =======================================================================

:: 1. Release any lingering elevated eden.exe lock before linking
taskkill /F /IM eden.exe /T >nul 2>&1

:: 2. Verify build_stage0 Ninja tree exists; otherwise bootstrap via Stage 0LD
if not exist "%~dp0..\build_stage0\build.ninja" (
    echo [WARN] No existing build_stage0 tree found. Falling back to Stage 0LD...
    call "%~dp0PGO_stage0LD.bat"
    exit /b %ERRORLEVEL%
)

set "LOG_FILE=%~dp0..\build_stage0_incremental.log"
echo [STATUS] Compiling modified C++20 targets via Ninja (-j 4)...
ninja -C "%~dp0..\build_stage0" -j 4 eden > "%LOG_FILE%" 2>&1
if %ERRORLEVEL% neq 0 (
    echo [@=FATAL] Incremental build failed. Check %LOG_FILE% for diagnostics.
    type "%LOG_FILE%"
    exit /b 1
)

:: 3. Re-stage runtime configs alongside updated binary
if exist "%~dp0..\ax_engine.ini" copy /Y "%~dp0..\ax_engine.ini" "%~dp0..\build_stage0\bin\ax_engine.ini" >nul 2>&1
if exist "%~dp0..\eden_engine.ini" copy /Y "%~dp0..\eden_engine.ini" "%~dp0..\build_stage0\bin\eden_engine.ini" >nul 2>&1
if exist "%~dp0..\src\audio_core\FlexASIO.toml" copy /Y "%~dp0..\src\audio_core\FlexASIO.toml" "%~dp0..\build_stage0\bin\FlexASIO.toml" >nul 2>&1

echo [PASS] Stage 0I incremental build successful: %~dp0..\build_stage0\bin\eden.exe
exit /b 0
```[cite: 14]

#### `Scripts/PGO_stage2.bat`

```batch
@echo off
setlocal enabledelayedexpansion
title Eden Emulator Pipeline - Stage 2 (Enterprise Production Optimizer)

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"

set "SCRIPT_DIR=%~dp0"
for %%i in ("%SCRIPT_DIR%..") do set "PROJ_DIR=%%~fi"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"
set "LLVM_PROJ_DIR=%PROJ_DIR:\=/%"

call "%SCRIPT_DIR%resolve_ax_tools.bat"
if !errorlevel! neq 0 exit /b 1

set "VSCMD_ARG_TGT_ARCH=x64"
set "TARGET_CPU_ARCH=Coffee Lake"
set "TARGET_GPU_VENDOR=NVIDIA"
set "TARGET_GPU_ARCH=Maxwell"
set "PIPELINE_STAGE=Stage2_Production_Optimization"

net session >nul 2>&1
if !errorlevel! neq 0 (
    echo [@=FATAL] INSUFFICIENT SYSTEM ADMINISTRATIVE PRIVILEGES DETECTED!
    pause
    exit /b 1
)

set "PATH=C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;C:\Program Files\ninja-win;C:\Program Files\Git\cmd;C:\Program Files\Git\bin;%PATH%"
set "QT_ENABLE_HIGHDPI_SCALING=1"
set "QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough"

if not defined BASE_GAME_DIR set "BASE_GAME_DIR=J:\Switch"
if not defined SSD_STAGING_DIR set "SSD_STAGING_DIR=E:\updates"
if not exist "%BASE_GAME_DIR%" echo [WARN] Mechanical archive directory (%BASE_GAME_DIR%) not mounted - continuing build.
if not exist "%SSD_STAGING_DIR%" mkdir "%SSD_STAGING_DIR%" 2>nul

cd /d "%PROJ_DIR%"

echo [STATUS] Verifying hardware topology and macro configuration...
powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_TOOLS_DIR%\check_hardware.ps1" -TargetApp eden -RepoRoot "%PROJ_DIR%"
if !errorlevel! neq 0 exit /b !errorlevel!

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%macro_update.ps1"
if errorlevel 1 (
    echo [ERROR] Macro update failed. Aborting.
    exit /b 1
)

set "BUILD_DIR=%PROJ_DIR%\build_stage2"
set "VERBOSE_LOG=%PROJ_DIR%\build_stage2_verbose.log"
set "PGO_PROFDATA=%LLVM_PROJ_DIR%/pgo_data/default.profdata"

if exist "%VERBOSE_LOG%" del /f /q "%VERBOSE_LOG%" >nul 2>&1
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if exist "%SCRIPT_DIR%validate_profile.ps1" (
    echo [STATUS] Validating LLVM PGO profile counters via validate_profile.ps1...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%validate_profile.ps1"
    if !errorlevel! neq 0 exit /b !errorlevel!
)
if not exist "%PROJ_DIR%\pgo_data\default.profdata" (
    echo [@=FATAL] PGO profile missing: %PROJ_DIR%\pgo_data\default.profdata — run Stage 1 sweep and llvm-profdata merge first.
    exit /b 1
)

set "OPT_FLAGS=/MD /O3 /Ob3 /Oi /Ot /Gw /Gy /DNDEBUG /D_WIN32_WINNT=0x0A00 /fp:fast /std:c++20 /Zm2000 -Qunused-arguments -Xclang -faligned-allocation -D_CRT_ALIGNMENT=32 -march=skylake -mavx2 -mfma -mbmi -mbmi2 -fprofile-use=%PGO_PROFDATA% -clang:-Wno-profile-instr-out-of-date -Xclang -fcxx-exceptions"
set "C_OPT_FLAGS=/MD /O3 /Ob3 /Oi /Ot /Gw /Gy /DNDEBUG /D_WIN32_WINNT=0x0A00 /fp:fast -Qunused-arguments -march=skylake -fprofile-use=%PGO_PROFDATA%"
set "LINKER_FLAGS=/IGNORE:4006 /IGNORE:4221 /errorlimit:20 /MAP:%BUILD_DIR%\eden_stage2.map /DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /DEFAULTLIB:ucrt.lib /DEFAULTLIB:bufferoverflowu.lib /DEFAULTLIB:mincore.lib /DEFAULTLIB:ws2_32.lib /OPT:REF /OPT:ICF ws2_32.lib"

echo [STATUS] Configuring Final PGO Profile-Guided Optimized CMake Build via Ninja...
cmake -G Ninja -Wno-unused-cli -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_LINKER="C:/Program Files/LLVM/bin/lld-link.exe" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_CXX_FLAGS_RELEASE="%OPT_FLAGS%" -DCMAKE_C_FLAGS_RELEASE="%C_OPT_FLAGS%" -DCMAKE_EXE_LINKER_FLAGS_RELEASE="%LINKER_FLAGS%" -DOPUS_FIXED_POINT=OFF -DOPUS_FLOAT_APPROX=OFF -DBUILD_TESTING=OFF -S "%PROJ_DIR%" -B "%BUILD_DIR%" > "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :cmake_fail

cd /d "%BUILD_DIR%"
if not exist "%BUILD_DIR%\build.ninja" goto :cmake_fail

echo [STATUS] Compiling PGO production release targets (-j 4)...
ninja -j 4 >> "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :ninja_fail
if not exist "%BUILD_DIR%\bin\eden.exe" goto :ninja_fail

echo [STATUS] Running post-build telemetry regression check...
powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_TOOLS_DIR%\check_regression.ps1" -RepoRoot "%PROJ_DIR%" -BaselineJson "%PROJ_DIR%\baseline_telemetry.json" -CurrentJson "%PROJ_DIR%\build_stage_telemetry_metrics.json"
if !errorlevel! neq 0 goto :ninja_fail

echo.
echo [STATUS] Engine linkage successful. Running distributor provisioning checks...
if not exist "%PROJ_DIR%\ax_engine.ini" (
    echo [STATUS] Local template profile missing. Generating fallback ax_engine.ini configuration layout...
    (
        echo [EngineTopology]
        echo EnableThreadAffinityMasks=true
        echo EnforceStrictCoreClamps=true
        echo MinCoresForIsolationSplit=6
        echo EnableProportionalScaler=true
        echo CoreRatio=2:3:1
        echo.
        echo [HardwareFeatureFlags]
        echo MaxwellSafeUBOPassthrough=auto
        echo IntelQSVVideoOffload=auto
        echo VramShieldPruneTriggerMB=5000
        echo VramShieldCeilingMB=5500
        echo VramShieldLsfgCeilingMB=5350
        echo Bar1StagingCapMB=128
        echo.
        echo [WindowsPlatformHacks]
        echo EnableMMCSSProAudioScaling=true
        echo HostTimingPriorityClass=HIGH
        echo VulkanDriverPriorityClass=HIGH
        echo GuestExecutionPriorityClass=ABOVENORMAL
        echo AuxQuarantinePriorityClass=BELOWNORMAL
    ) > "%PROJ_DIR%\ax_engine.ini"
)

echo [STATUS] Deploying configuration components downstream alongside binary targets...
copy /Y "%PROJ_DIR%\ax_engine.ini" "%BUILD_DIR%\bin\ax_engine.ini" >nul
if exist "%PROJ_DIR%\eden_engine.ini" copy /Y "%PROJ_DIR%\eden_engine.ini" "%BUILD_DIR%\bin\eden_engine.ini" >nul
if exist "%PROJ_DIR%\src\audio_core\FlexASIO.toml" (
    copy /Y "%PROJ_DIR%\src\audio_core\FlexASIO.toml" "%BUILD_DIR%\bin\FlexASIO.toml" >nul
    echo [STATUS] Pre-staged FlexASIO.toml deployment map finalized.
)

echo.
echo -----------------------------------------------------------------------
echo [STATUS] Engine optimization compilation pipeline verification successful.
echo -----------------------------------------------------------------------
echo %ESC%[r
choice /c YN /n /m " -> View final optimization feedback log traces? [Y/N]: "
if errorlevel 2 goto :exit_pipeline
if errorlevel 1 notepad "%VERBOSE_LOG%"

:exit_pipeline
exit /b 0

:cmake_fail
echo %ESC%[r
echo [@=FATAL] SYSTEM GENERATION ERROR - CMake configuration failed.
exit /b 1

:ninja_fail
echo %ESC%[r
echo [@=FATAL] COMPILATION ERROR - Stage 2 Optimization build compilation dropped out.
exit /b 1
```[cite: 17]

*(Keeping the 150 GB partition and USB installer parked until you're ready.)*

Want to drop in any other browser files from `ax-sdk` or `FLAC_AX` next, or run your local `Eden-Espresso-Wave-AX` build?
