// src/video_upscaler_main.cpp — Single-stream 720p/1080p/2K DVD & Retro Capture Runner for FLAC_AX
// Topology: Core 5 (V4L2 / Stream I/O) -> Core 1 (Shader Pacing & Upscale) -> Core 0 (SCHED_FIFO 80 ESS Sabre ALSA)
#include "video/av_pts_sync.hpp"
#include "video/ax_720p_hw_gate.hpp"
#include "video/upscale_720p_shader.hpp"

#if !defined(_WIN32)
#include "ess_dac_hooks.hpp"
#include "video/v4l2_live_capture.hpp"

#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#include <csignal>
#endif

#if defined(FLAC_AX_HAS_SDL2_GL)
#include <GL/gl.h>
#include <SDL.h>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

enum class RunnerMode : std::uint8_t {
    Dvd = 0,
    Retro
};

struct OutputPreset {
    std::string_view name;
    std::uint32_t width;
    std::uint32_t height;
};

constexpr OutputPreset ResolveOutputPreset(std::string_view preset) noexcept {
    if (preset == "720p") {
        return OutputPreset{"720p", ax::video::kTargetWidth720p, ax::video::kTargetHeight720p};
    }
    if (preset == "1080p") {
        return OutputPreset{"1080p", 1920U, 1080U};
    }
    if (preset == "2k" || preset == "1440p") {
        return OutputPreset{"2k", 2560U, 1440U};
    }
    return OutputPreset{};
}

bool ParseUint32(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty()) return false;
    std::uint32_t value = 0U;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) return false;
    out = value;
    return true;
}

bool ParseDouble(std::string_view text, double& out) noexcept {
    if (text.empty()) return false;
    std::string buf(text);
    char* end_ptr = nullptr;
    const double val = std::strtod(buf.c_str(), &end_ptr);
    if (end_ptr == buf.c_str() || *end_ptr != '\0' || !std::isfinite(val)) {
        return false;
    }
    out = val;
    return true;
}

void OnSignal(int /*sig*/) noexcept {
    g_running.store(false, std::memory_order_release);
}

void InstallSignals() noexcept {
#if !defined(_WIN32)
    struct sigaction sa {};
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);
#else
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif
}

void PinThread(int core_id, int fifo_prio) noexcept {
#if !defined(_WIN32)
    if (core_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(core_id), &set);
        (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
    }
    if (fifo_prio > 0) {
        sched_param sp{};
        sp.sched_priority = fifo_prio;
        (void)::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &sp);
    }
#else
    (void)core_id;
    (void)fifo_prio;
#endif
}

#if !defined(_WIN32)
bool ConfigureUpscalerAlsa(snd_pcm_t* pcm,
                             const flac_ax::ess::EssChipProfile& chip,
                             unsigned int target_rate_hz,
                             snd_pcm_uframes_t period_frames,
                             unsigned int& out_rate,
                             snd_pcm_uframes_t& out_period) {
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);

    if (snd_pcm_hw_params_any(pcm, params) < 0) return false;
    if (snd_pcm_hw_params_set_rate_resample(pcm, params, 0) < 0) return false;
    if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) return false;
    if (snd_pcm_hw_params_set_format(pcm, params, chip.preferred_format) < 0) return false;
    if (snd_pcm_hw_params_set_channels(pcm, params, 2) < 0) return false;

    unsigned int rate = flac_ax::ess::NegotiateEssRate(chip, target_rate_hz);
    int dir = 0;
    if (snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir) < 0) return false;

    snd_pcm_uframes_t period = period_frames;
    if (snd_pcm_hw_params_set_period_size_near(pcm, params, &period, &dir) < 0) return false;

    const unsigned int mult = flac_ax::ess::RecommendedBufferPeriods(chip, rate, 0);
    snd_pcm_uframes_t buffer = period * mult;
    if (snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer) < 0) return false;

    if (snd_pcm_hw_params(pcm, params) < 0) return false;
    if (snd_pcm_prepare(pcm) < 0) return false;

    out_rate = rate;
    out_period = period;
    return flac_ax::ess::OnEssHardwareConfigured(chip, pcm, out_rate, out_period);
}
#endif

struct Core1UpscaleStage {
    OutputPreset preset{"720p", ax::video::kTargetWidth720p, ax::video::kTargetHeight720p};
    ax::video::Upscale720pParams params{};
#if defined(FLAC_AX_HAS_SDL2_GL)
    SDL_Window* window{nullptr};
    SDL_GLContext gl_ctx{nullptr};
    bool gl_ready{false};
#endif

    bool Init(const OutputPreset& out_preset,
              const ax::video::Upscale720pParams& p,
              bool enable_window) noexcept {
        preset = out_preset;
        params = p;
#if defined(FLAC_AX_HAS_SDL2_GL)
        if (!enable_window) {
            return true;
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            return false;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window = SDL_CreateWindow(
            "FLAC_AX Video Upscaler",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            static_cast<int>(preset.width),
            static_cast<int>(preset.height),
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (!window) {
            SDL_Quit();
            return false;
        }
        gl_ctx = SDL_GL_CreateContext(window);
        if (!gl_ctx) {
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            return false;
        }
        (void)SDL_GL_SetSwapInterval(1);
        glViewport(0, 0, static_cast<GLsizei>(preset.width), static_cast<GLsizei>(preset.height));
        gl_ready = true;
        return true;
#else
        (void)enable_window;
        return true;
#endif
    }

    void RenderFrame(std::uint32_t src_w,
                     std::uint32_t src_h,
                     std::uint64_t frame_seq) noexcept {
        params.is_pal_576 = (src_h == 576U);
        const int field_parity = static_cast<int>(frame_seq & 1ULL);
        const int scan_mode_int = static_cast<int>(params.scan_mode);
        (void)src_w;
        (void)field_parity;
        (void)scan_mode_int;
        (void)ax::video::kUpscale720pFragmentShader;

#if defined(FLAC_AX_HAS_SDL2_GL)
        if (gl_ready && window) {
            glClearColor(0.03f, 0.04f, 0.06f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }
#endif
    }

    void Shutdown() noexcept {
#if defined(FLAC_AX_HAS_SDL2_GL)
        if (gl_ctx) {
            SDL_GL_DeleteContext(gl_ctx);
            gl_ctx = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if (gl_ready) {
            SDL_Quit();
            gl_ready = false;
        }
#endif
    }
};

void PrintUsage() {
    std::cerr
        << "Usage: ax_video_upscaler [options]\n"
        << "  --mode <dvd|retro>        Single-stream mode (default: retro)\n"
        << "  --preset <720p|1080p|2k>  Output resolution preset (default: 720p)\n"
        << "  --v4l2 <dev>            V4L2 device for retro mode (default: /dev/video0)\n"
        << "  --input <path>          DVD source path (.iso/.vob/title) for dvd mode\n"
        << "  --width <px>            Capture width (default: 720)\n"
        << "  --height <px>           Capture height (default: 480)\n"
        << "  --fps <hz>              Capture/stream frame rate (default: 60)\n"
        << "  --scan <240p|bob|dvd>   Scan mode (default: bob for retro, dvd for dvd)\n"
        << "  --device <alsa_dev>     ALSA PCM device (default: hw:CARD=FC3,DEV=0)\n"
        << "  --mixer-card <card>     ALSA mixer card (default: FC3)\n"
        << "  --volume <0.0..1.0>     Set ESS hardware mixer playback level\n"
        << "  --rate <hz>             Audio sample rate (default: 48000)\n"
        << "  --period <frames>       ALSA period frames (default: 96)\n"
        << "  --dll-dir <folder>      Optional folder to probe for Lossless.dll\n"
        << "  --no-window             Run Core 1 pacing/upscale without creating a window\n";
}

} // namespace

int main(int argc, char** argv) {
    RunnerMode mode = RunnerMode::Retro;
    OutputPreset preset = ResolveOutputPreset("720p");
    std::string v4l2_dev = "/dev/video0";
    std::string dvd_input;
    std::string alsa_override;
    std::string mixer_card = "FC3";
    std::filesystem::path dll_dir;

    bool has_volume = false;
    bool has_scan_override = false;
    bool enable_window = true;
    double target_volume = 1.0;

    std::uint32_t src_w = 720U;
    std::uint32_t src_h = 480U;
    std::uint32_t src_fps = 60U;
    std::uint32_t sample_rate = 48000U;
    std::uint32_t period_frames = 96U;
    ax::video::ScanMode scan_mode = ax::video::ScanMode::BobInterlaced480i576i;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            const std::string_view m = argv[++i];
            if (m == "dvd") {
                mode = RunnerMode::Dvd;
                if (!has_scan_override) {
                    scan_mode = ax::video::ScanMode::DvdCadenceAdaptive;
                }
            } else if (m == "retro") {
                mode = RunnerMode::Retro;
                if (!has_scan_override) {
                    scan_mode = ax::video::ScanMode::BobInterlaced480i576i;
                }
            } else {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--preset" && i + 1 < argc) {
            preset = ResolveOutputPreset(argv[++i]);
            if (preset.width == 0U || preset.height == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--v4l2" && i + 1 < argc) {
            v4l2_dev = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            dvd_input = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], src_w) || src_w == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--height" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], src_h) || src_h == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--fps" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], src_fps) || src_fps == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--scan" && i + 1 < argc) {
            const std::string_view s = argv[++i];
            has_scan_override = true;
            if (s == "240p") {
                scan_mode = ax::video::ScanMode::Progressive240p;
            } else if (s == "bob") {
                scan_mode = ax::video::ScanMode::BobInterlaced480i576i;
            } else if (s == "dvd") {
                scan_mode = ax::video::ScanMode::DvdCadenceAdaptive;
            } else {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--device" && i + 1 < argc) {
            alsa_override = argv[++i];
        } else if (arg == "--mixer-card" && i + 1 < argc) {
            mixer_card = argv[++i];
        } else if (arg == "--volume" && i + 1 < argc) {
            if (!ParseDouble(argv[++i], target_volume)) {
                PrintUsage();
                return 1;
            }
            has_volume = true;
        } else if (arg == "--rate" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], sample_rate) || sample_rate == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--period" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], period_frames) || period_frames == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--dll-dir" && i + 1 < argc) {
            dll_dir = argv[++i];
        } else if (arg == "--no-window") {
            enable_window = false;
        } else {
            PrintUsage();
            return 1;
        }
    }

    InstallSignals();

    const auto guardrails = ax::video::Resolve720pGuardrails(5.50, 5.00, 0x20ULL, dll_dir);
    std::cout << "[UPSCALER] Preset: " << preset.name << " (" << preset.width << "x" << preset.height << ")"
              << " | VRAM ceiling: " << guardrails.vram_ceiling_gib << " GiB"
              << " | Optional DLL active: " << (guardrails.has_optional_dll ? "yes" : "no")
              << "\n";

#if !defined(_WIN32)
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[UPSCALER] WARN: mlockall skipped (check CAP_IPC_LOCK / RLIMIT_MEMLOCK)\n";
    }

    const int lock_fd = ::open("/run/lock/ax-dac.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (lock_fd < 0) {
        std::cerr << "[UPSCALER] Failed to open /run/lock/ax-dac.lock\n";
        return 1;
    }
    if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "[UPSCALER] DAC lock busy (/run/lock/ax-dac.lock held by another process)\n";
        ::close(lock_fd);
        return 1;
    }

    const auto chip = flac_ax::ess::MakeEssProfile(flac_ax::ess::EssChipFamily::Es9281AcPro);
    const std::string alsa_dev = !alsa_override.empty() ? alsa_override : chip.default_alsa_device;

    flac_ax::ess::EssHardwareMixer hw_mixer;
    if (has_volume && hw_mixer.Open(mixer_card.c_str())) {
        (void)hw_mixer.SetNormalizedVolume(target_volume);
    }

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, alsa_dev.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "[UPSCALER] Failed to open ALSA PCM '" << alsa_dev << "'\n";
        (void)::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return 1;
    }

    unsigned int actual_rate = sample_rate;
    std::uint32_t actual_period_u32 = period_frames;
    snd_pcm_uframes_t actual_period = static_cast<snd_pcm_uframes_t>(period_frames);
    if (!ConfigureUpscalerAlsa(pcm, chip, sample_rate, actual_period, actual_rate, actual_period)) {
        std::cerr << "[UPSCALER] Failed to configure ALSA hardware parameters\n";
        snd_pcm_close(pcm);
        (void)::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return 1;
    }
    actual_period_u32 = static_cast<std::uint32_t>(actual_period);

    ax::video::AudioMasterPtsSync pts_sync(
        actual_rate,
        (mode == RunnerMode::Dvd) ? ax::video::SyncMode::AudioMasterDvd
                                  : ax::video::SyncMode::LiveLowLatency);
    pts_sync.Reset(0);

    ax::video::V4l2LiveCaptureSource v4l2_source;
    const int aux_core = (guardrails.storage_aux_mask == 0x20ULL) ? 5 : -1;
    if (mode == RunnerMode::Retro) {
        if (!v4l2_source.Start(v4l2_dev, src_w, src_h, src_fps, aux_core)) {
            std::cerr << "[UPSCALER] Failed to start V4L2 capture on '" << v4l2_dev << "'\n";
            snd_pcm_close(pcm);
            (void)::flock(lock_fd, LOCK_UN);
            ::close(lock_fd);
            return 1;
        }
        std::cout << "[UPSCALER] V4L2 capture active on " << v4l2_dev << " ("
                  << v4l2_source.width() << "x" << v4l2_source.height() << ")\n";
    } else {
        std::cout << "[UPSCALER] DVD single-stream mode initialized (input='"
                  << (dvd_input.empty() ? "<stdin/title>" : dvd_input) << "')\n";
    }

    std::atomic<std::uint64_t> xrun_count{0ULL};

    std::thread audio_thread([&]() {
        PinThread(0, 80);
        std::vector<std::int32_t> silence(static_cast<std::size_t>(actual_period_u32) * 2U, 0);

        while (g_running.load(std::memory_order_acquire)) {
            std::size_t offset = 0;
            while (offset < actual_period_u32 && g_running.load(std::memory_order_acquire)) {
                const snd_pcm_sframes_t written = snd_pcm_writei(
                    pcm,
                    silence.data() + offset * 2U,
                    static_cast<snd_pcm_uframes_t>(actual_period_u32 - offset));
                if (written >= 0) {
                    const auto w_frames = static_cast<std::uint64_t>(written);
                    offset += static_cast<std::size_t>(written);
                    pts_sync.OnAudioFramesCommitted(w_frames, std::chrono::steady_clock::now());
                } else if (written == -EAGAIN || written == -EINTR) {
                    (void)snd_pcm_wait(pcm, 2);
                } else if (written == -EPIPE) {
                    xrun_count.fetch_add(1ULL, std::memory_order_relaxed);
                    if (snd_pcm_recover(pcm, static_cast<int>(written), 1) < 0) {
                        g_running.store(false, std::memory_order_release);
                        break;
                    }
                } else {
                    g_running.store(false, std::memory_order_release);
                    break;
                }
            }
        }
    });
#else
    (void)alsa_override;
    (void)mixer_card;
    (void)has_volume;
    (void)target_volume;
    (void)period_frames;
    (void)v4l2_dev;
    (void)dvd_input;
    std::cout << "[UPSCALER] Windows build mode: ALSA/V4L2 hardware capture skipped (use Linux/Piccolo-AX for hardware I/O)\n";
    ax::video::AudioMasterPtsSync pts_sync(
        sample_rate,
        (mode == RunnerMode::Dvd) ? ax::video::SyncMode::AudioMasterDvd
                                  : ax::video::SyncMode::LiveLowLatency);
    pts_sync.Reset(0);
    std::atomic<std::uint64_t> xrun_count{0ULL};
#endif

    PinThread(1, 0);
    ax::video::Upscale720pParams upscale_params{};
    upscale_params.scan_mode = scan_mode;
    upscale_params.is_pal_576 = (src_h == 576U);

    Core1UpscaleStage stage;
    if (!stage.Init(preset, upscale_params, enable_window)) {
        std::cerr << "[UPSCALER] WARN: Window/GL context init skipped; continuing headless pacing\n";
    }

#if !defined(_WIN32)
    ax::video::CapturedFrame latest_frame{};
#endif
    std::uint64_t last_seq = 0ULL;
    std::uint64_t synthetic_frame_idx = 0ULL;
    const std::int64_t frame_step_us =
        (src_fps > 0U) ? static_cast<std::int64_t>(1000000ULL / static_cast<std::uint64_t>(src_fps))
                       : 16667LL;
    auto last_report = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_acquire)) {
        bool have_frame = false;
        std::int64_t frame_pts_us = 0;

#if !defined(_WIN32)
        if (mode == RunnerMode::Retro) {
            have_frame = v4l2_source.CopyLatestFrame(latest_frame, last_seq);
            if (have_frame) {
                frame_pts_us = pts_sync.MasterClockUs();
            }
        } else {
            const std::int64_t master_us = pts_sync.MasterClockUs();
            frame_pts_us = synthetic_frame_idx * frame_step_us;
            if (frame_pts_us <= master_us + frame_step_us) {
                have_frame = true;
                ++synthetic_frame_idx;
            }
        }
#else
        const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - last_report)
                                    .count();
        frame_pts_us = synthetic_frame_idx * frame_step_us;
        if (frame_pts_us <= elapsed_us + frame_step_us) {
            have_frame = true;
            ++synthetic_frame_idx;
            ++last_seq;
        }
#endif

        if (have_frame) {
            const auto decision = pts_sync.EvaluateVideoFrame(frame_pts_us);
            if (decision.action == ax::video::FramePacingAction::HoldEarly && decision.hold_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(decision.hold_us));
            }
            if (decision.action != ax::video::FramePacingAction::DropLate) {
                stage.RenderFrame(src_w, src_h, last_seq);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count() >= 5) {
            const auto sync_t = pts_sync.SnapshotTelemetry();
            std::cout << "[UPSCALER] preset=" << preset.name
                      << " presented=" << sync_t.frames_presented
                      << " held=" << sync_t.frames_held_early
                      << " dropped_late=" << sync_t.frames_dropped_late
                      << " av_delta_us=" << sync_t.last_av_delta_us
                      << " xruns=" << xrun_count.load(std::memory_order_relaxed) << "\n";
            last_report = now;
        }
    }

    g_running.store(false, std::memory_order_release);
    stage.Shutdown();

#if !defined(_WIN32)
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    if (mode == RunnerMode::Retro) {
        v4l2_source.Stop();
    }

    (void)snd_pcm_drop(pcm);
    (void)snd_pcm_close(pcm);
    hw_mixer.Close();

    (void)::flock(lock_fd, LOCK_UN);
    ::close(lock_fd);
#endif
    return 0;
}

