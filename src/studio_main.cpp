// src/studio_main.cpp — Unified [AX] Audio/Visual Synth, Mixer & Spectrum Studio
// Topology: Core 5 (MPK Mini MK3 MIDI) -> Cores 2-4 (AVX2 DSP + FFT) -> Core 0 (ESS Sabre ALSA) & Core 1 (GTX 980 Ti HUD)
#include "dsp/avx2_synth.hpp"
#include "ess_dac_hooks.hpp"
#include "midi/mpk_mini3.hpp"
#include "visual/av_hud.hpp"

#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/config.hpp>) && __has_include(<ax/telemetry.hpp>)
#include <ax/config.hpp>
#include <ax/telemetry.hpp>
#define FLAC_AX_STUDIO_HAVE_TELEMETRY 1
#endif
#endif

namespace {

std::atomic<bool> g_running{true};

void OnSignal(int /*sig*/) noexcept {
    g_running.store(false, std::memory_order_release);
}

void InstallSignals() noexcept {
    struct sigaction sa {};
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr); // Yield DAC cleanly when an emulator launches
}

void PinThread(int core_id, int fifo_prio) noexcept {
    if (core_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(core_id), &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    if (fifo_prio > 0) {
        sched_param sp{};
        sp.sched_priority = fifo_prio;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    }
}

bool ConfigureStudioAlsa(snd_pcm_t* pcm,
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

    snd_pcm_uframes_t buffer = period * chip.default_buffer_periods;
    if (snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer) < 0) return false;

    if (snd_pcm_hw_params(pcm, params) < 0) return false;
    if (snd_pcm_prepare(pcm) < 0) return false;

    out_rate = rate;
    out_period = period;
    return flac_ax::ess::OnEssHardwareConfigured(chip, pcm, out_rate, out_period);
}

} // namespace

int main(int argc, char** argv) {
    bool fullscreen = false;
    bool headless = false;
    std::string device_override;
    std::string midi_override;
    unsigned int sample_rate = 48000U;
    snd_pcm_uframes_t period_frames = 96U; // 2.00 ms @ 48 kHz

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--device" && i + 1 < argc) {
            device_override = argv[++i];
        } else if (arg == "--midi" && i + 1 < argc) {
            midi_override = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            sample_rate = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (arg == "--period" && i + 1 < argc) {
            period_frames = static_cast<snd_pcm_uframes_t>(std::stoul(argv[++i]));
        }
    }

    InstallSignals();
    mlockall(MCL_CURRENT | MCL_FUTURE);

#if defined(FLAC_AX_STUDIO_HAVE_TELEMETRY)
    ax::EngineConfig ax_cfg{};
    (void)ax::LoadEngineConfig("ax_engine.ini", ax_cfg);
    if (ax_cfg.enable_shared_memory_export) {
        (void)ax::InitTelemetryShm();
    }
    ax::ResetTelemetry();
#endif

    // Acquire shared DAC arbitration lock (/run/lock/ax-dac.lock)
    const int lock_fd = ::open("/run/lock/ax-dac.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (lock_fd >= 0) {
        (void)::flock(lock_fd, LOCK_EX | LOCK_NB);
    }

    const auto chip = flac_ax::ess::MakeEssProfile(flac_ax::ess::EssChipFamily::Es9281AcPro);
    const std::string alsa_dev = !device_override.empty() ? device_override : chip.default_alsa_device;

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, alsa_dev.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "[STUDIO] Failed to open ESS Sabre ALSA device '" << alsa_dev << "'\n";
        if (lock_fd >= 0) ::close(lock_fd);
        return 1;
    }

    unsigned int actual_rate = sample_rate;
    snd_pcm_uframes_t actual_period = period_frames;
    if (!ConfigureStudioAlsa(pcm, chip, sample_rate, period_frames, actual_rate, actual_period)) {
        std::cerr << "[STUDIO] Failed to configure ALSA hardware parameters\n";
        snd_pcm_close(pcm);
        if (lock_fd >= 0) ::close(lock_fd);
        return 1;
    }

    std::cout << "[STUDIO] ESS Sabre (" << chip.chip_name << ") locked at "
              << actual_rate << " Hz, period=" << actual_period << " frames ("
              << (static_cast<double>(actual_period) * 1000.0 / static_cast<double>(actual_rate)) << " ms)\n";

    // 1. Start Core 5 (0x20) Akai MPK Mini MK3 RawMIDI listener
    flac_ax::midi::MpkMini3Listener mpk_listener;
    mpk_listener.Start(5, midi_override);

    // 2. Initialize DSP synth & shared telemetry
    flac_ax::dsp::PolySynthEngine synth(actual_rate);
    std::atomic<std::uint64_t> xrun_count{0};
    std::atomic<std::uint32_t> jitter_p99{0};

    // 3. Start Core 0 (0x01 / SCHED_FIFO 80) real-time ALSA writer + DSP render loop
    std::thread audio_thread([&]() {
        PinThread(0, 80);
        std::vector<std::int32_t> period_buf(actual_period * 2, 0);
        flac_ax::midi::MidiEvent ev{};

        while (g_running.load(std::memory_order_acquire)) {
#if defined(FLAC_AX_STUDIO_HAVE_TELEMETRY)
            const auto t0 = std::chrono::steady_clock::now();
#endif
            // Drain all pending MPK Mini MK3 events lock-free
            while (mpk_listener.PopEvent(ev)) {
                synth.ProcessMidiEvent(ev);
            }

            synth.RenderInterleavedS32(period_buf.data(), actual_period);

            std::size_t offset = 0;
            while (offset < actual_period && g_running.load(std::memory_order_acquire)) {
                const snd_pcm_sframes_t written =
                    snd_pcm_writei(pcm, period_buf.data() + offset * 2, actual_period - offset);
                if (written >= 0) {
                    offset += static_cast<std::size_t>(written);
                } else if (written == -EAGAIN) {
                    snd_pcm_wait(pcm, 2);
                } else if (written == -EPIPE) {
                    xrun_count.fetch_add(1, std::memory_order_relaxed);
                    if (snd_pcm_recover(pcm, static_cast<int>(written), 1) < 0) {
                        g_running.store(false, std::memory_order_release);
                        break;
                    }
                } else {
                    g_running.store(false, std::memory_order_release);
                    break;
                }
            }

#if defined(FLAC_AX_STUDIO_HAVE_TELEMETRY)
            const auto us = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            ax::RecordAudioCallbackMetrics(us, static_cast<std::uint32_t>(actual_period));
            jitter_p99.store(ax::GetTelemetrySnapshot().audio_jitter_p99_us, std::memory_order_relaxed);
#endif
        }
    });

    // 4. Run Core 1 (0x02) VBlank-locked GTX 980 Ti OpenGL Studio HUD on main thread
    if (!headless) {
        flac_ax::visual::AvStudioHud hud;
        if (hud.Init(1, fullscreen)) {
            while (g_running.load(std::memory_order_acquire)) {
                flac_ax::visual::StudioTelemetry telem{};
                telem.sample_rate_hz = actual_rate;
                telem.period_frames = static_cast<unsigned int>(actual_period);
                telem.xruns = xrun_count.load(std::memory_order_relaxed);
                telem.jitter_p99_us = jitter_p99.load(std::memory_order_relaxed);
                telem.dac_locked = true;
                telem.mpk_connected = mpk_listener.state().connected.load(std::memory_order_relaxed);

                if (!hud.RenderFrame(synth.ReadVisualSnapshot(), mpk_listener.state(), telem)) {
                    g_running.store(false, std::memory_order_release);
                    break;
                }
            }
            hud.Shutdown();
        }
    } else {
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    g_running.store(false, std::memory_order_release);
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    mpk_listener.Stop();

    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    if (lock_fd >= 0) {
        (void)::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
    }

#if defined(FLAC_AX_STUDIO_HAVE_TELEMETRY)
    ax::ShutdownTelemetryShm();
#endif
    return 0;
}
