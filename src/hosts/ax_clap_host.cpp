// C++20 CLAP single-plugin host wired to StereoFloatRing, ScopedDacLock, and real sink drain.
// Target: Piccolo-AX / i5-9600K (2:3:1 topology: DSP on Cores 2-4, DAC output on Core 0).

#include "audio/ax_dac_lock.hpp"
#include "audio/ax_stereo_ring.hpp"

#include <clap/clap.h>

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#define AX_CLAP_HAS_ALSA 1
#else
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/telemetry.hpp>)
#include <ax/telemetry.hpp>
#define AX_CLAP_HAS_TELEMETRY 1
#endif
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) noexcept {
    g_running.store(false, std::memory_order_release);
}

void pin_thread_mask(std::uint64_t cpu_mask, int fifo_prio = 0) noexcept {
#if !defined(_WIN32)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int cpu = 0; cpu < 64; ++cpu) {
        if ((cpu_mask >> cpu) & 1ULL) {
            CPU_SET(cpu, &cpuset);
        }
    }
    (void)::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (fifo_prio > 0) {
        sched_param sp{};
        sp.sched_priority = fifo_prio;
        (void)::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &sp);
    }
#else
    (void)cpu_mask;
    (void)fifo_prio;
#endif
}

struct SharedModuleHandle {
#if !defined(_WIN32)
    void* handle{nullptr};
#else
    HMODULE handle{nullptr};
#endif

    bool open(const char* path) noexcept {
#if !defined(_WIN32)
        handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        return handle != nullptr;
#else
        handle = ::LoadLibraryA(path);
        return handle != nullptr;
#endif
    }

    const clap_plugin_entry* entry() const noexcept {
#if !defined(_WIN32)
        if (!handle) return nullptr;
        return reinterpret_cast<const clap_plugin_entry*>(::dlsym(handle, "clap_entry"));
#else
        if (!handle) return nullptr;
        return reinterpret_cast<const clap_plugin_entry*>(
            reinterpret_cast<void*>(::GetProcAddress(handle, "clap_entry")));
#endif
    }

    void close() noexcept {
#if !defined(_WIN32)
        if (handle) {
            ::dlclose(handle);
            handle = nullptr;
        }
#else
        if (handle) {
            ::FreeLibrary(handle);
            handle = nullptr;
        }
#endif
    }
};

#if defined(AX_CLAP_HAS_ALSA)
class AlsaStereoSink {
public:
    ~AlsaStereoSink() noexcept { Close(); }

    bool Open(const char* device_name, unsigned int sample_rate, unsigned int period_frames) noexcept {
        Close();
        if (!device_name || device_name[0] == '\0') {
            return false;
        }
        if (snd_pcm_open(&pcm_, device_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
            pcm_ = nullptr;
            return false;
        }

        snd_pcm_hw_params_t* params = nullptr;
        snd_pcm_hw_params_alloca(&params);
        unsigned int rate = sample_rate;
        snd_pcm_uframes_t period = period_frames;
        snd_pcm_uframes_t buffer = period * 4U;
        int dir = 0;

        if (snd_pcm_hw_params_any(pcm_, params) < 0 ||
            snd_pcm_hw_params_set_rate_resample(pcm_, params, 0) < 0 ||
            snd_pcm_hw_params_set_access(pcm_, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
            snd_pcm_hw_params_set_format(pcm_, params, SND_PCM_FORMAT_S32_LE) < 0 ||
            snd_pcm_hw_params_set_channels(pcm_, params, 2) < 0 ||
            snd_pcm_hw_params_set_rate_near(pcm_, params, &rate, &dir) < 0 ||
            snd_pcm_hw_params_set_period_size_near(pcm_, params, &period, &dir) < 0 ||
            snd_pcm_hw_params_set_buffer_size_near(pcm_, params, &buffer) < 0 ||
            snd_pcm_hw_params(pcm_, params) < 0 ||
            snd_pcm_prepare(pcm_) < 0) {
            Close();
            return false;
        }
        return true;
    }

    bool WriteFrames(const std::int32_t* interleaved_s32, std::size_t frames) noexcept {
        if (!pcm_) return false;
        std::size_t offset = 0U;
        while (offset < frames && g_running.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm_, interleaved_s32 + offset * 2U, frames - offset);
            if (written >= 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written == -EAGAIN || written == -EINTR) {
                (void)snd_pcm_wait(pcm_, 2);
            } else if (written == -EPIPE) {
                ++xruns_;
                if (snd_pcm_recover(pcm_, static_cast<int>(written), 1) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return true;
    }

    void Close() noexcept {
        if (pcm_) {
            (void)snd_pcm_drain(pcm_);
            (void)snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    bool is_open() const noexcept { return pcm_ != nullptr; }
    std::uint64_t xruns() const noexcept { return xruns_; }

private:
    snd_pcm_t* pcm_{nullptr};
    std::uint64_t xruns_{0ULL};
};
#endif

const void* host_get_extension(const clap_host* /*host*/, const char* /*extension_id*/) {
    return nullptr;
}

void host_request_restart(const clap_host* /*host*/) {}

void host_request_process(const clap_host* /*host*/) {}

void host_request_callback(const clap_host* /*host*/) {}

void host_log(const clap_host* /*host*/, clap_log_severity severity, const char* msg) {
    if (msg) {
        std::fprintf(stderr, "[CLAP:%d] %s\n", static_cast<int>(severity), msg);
    }
}

void PrintUsage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s <path-to-plugin.clap> [plugin-id] [sample-rate] [period-frames] [--device <alsa_dev>] [--no-alsa]\n",
                 prog);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    const char* clap_path = argv[1];
    const char* requested_id = nullptr;
    double sample_rate = 48000.0;
    std::uint32_t period_frames = 96U;
    std::string alsa_device = "hw:CARD=FC3,DEV=0";
    bool use_alsa = true;

    int pos = 2;
    if (pos < argc && argv[pos][0] != '-') {
        if (argv[pos][0] != '\0') {
            requested_id = argv[pos];
        }
        ++pos;
    }
    if (pos < argc && argv[pos][0] != '-') {
        sample_rate = std::atof(argv[pos]);
        ++pos;
    }
    if (pos < argc && argv[pos][0] != '-') {
        period_frames = static_cast<std::uint32_t>(std::strtoul(argv[pos], nullptr, 10));
        ++pos;
    }
    for (int i = pos; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            alsa_device = argv[++i];
        } else if (arg == "--no-alsa") {
            use_alsa = false;
        } else {
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (sample_rate <= 0.0 || period_frames == 0U) {
        std::fprintf(stderr, "Invalid sample rate or period frames\n");
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    pin_thread_mask(0x1CULL, 0);

    flac_ax::audio::ScopedDacLock dac_lock;
    if (!dac_lock.Acquire("/run/lock/ax-dac.lock", false)) {
        std::fprintf(stderr, "Failed acquiring /run/lock/ax-dac.lock\n");
        return 1;
    }

    SharedModuleHandle mod;
    if (!mod.open(clap_path)) {
        std::fprintf(stderr, "Failed loading CLAP module: %s\n", clap_path);
        return 1;
    }

    const clap_plugin_entry* entry = mod.entry();
    if (!entry || !entry->init || !entry->deinit || !entry->get_factory) {
        std::fprintf(stderr, "Invalid clap_entry in %s\n", clap_path);
        mod.close();
        return 1;
    }

    if (!entry->init(clap_path)) {
        std::fprintf(stderr, "clap_plugin_entry::init failed for %s\n", clap_path);
        mod.close();
        return 1;
    }

    const auto* factory =
        static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || !factory->get_plugin_count || !factory->get_plugin_descriptor || !factory->create_plugin) {
        std::fprintf(stderr, "CLAP plugin factory unavailable in %s\n", clap_path);
        entry->deinit();
        mod.close();
        return 1;
    }

    const std::uint32_t count = factory->get_plugin_count(factory);
    const clap_plugin_descriptor* desc = nullptr;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto* d = factory->get_plugin_descriptor(factory, i);
        if (!d || !d->id) continue;
        if (!requested_id || std::strcmp(d->id, requested_id) == 0) {
            desc = d;
            break;
        }
    }

    if (!desc || !desc->id) {
        std::fprintf(stderr, "Requested CLAP plugin not found in %s\n", clap_path);
        entry->deinit();
        mod.close();
        return 1;
    }

    static const clap_host_log host_log_ext{host_log};
    static const clap_host host_iface{
        CLAP_VERSION,
        const_cast<clap_host_log*>(&host_log_ext),
        "FLAC_AX",
        "The Silicon Preservation Society",
        "https://github.com/galaxywarriordad/FLAC_AX",
        "1.36.2",
        host_get_extension,
        host_request_restart,
        host_request_process,
        host_request_callback,
    };

    const clap_plugin* plugin = factory->create_plugin(factory, &host_iface, desc->id);
    if (!plugin || !plugin->init || !plugin->destroy) {
        std::fprintf(stderr, "Failed creating CLAP plugin instance: %s\n", desc->id);
        entry->deinit();
        mod.close();
        return 1;
    }

    if (!plugin->init(plugin)) {
        std::fprintf(stderr, "CLAP plugin init failed: %s\n", desc->id);
        plugin->destroy(plugin);
        entry->deinit();
        mod.close();
        return 1;
    }

    std::vector<float> in_l(period_frames, 0.0f);
    std::vector<float> in_r(period_frames, 0.0f);
    std::vector<float> out_l(period_frames, 0.0f);
    std::vector<float> out_r(period_frames, 0.0f);

    float* in_ptrs[2]{in_l.data(), in_r.data()};
    float* out_ptrs[2]{out_l.data(), out_r.data()};

    clap_audio_buffer audio_in{};
    audio_in.data32 = in_ptrs;
    audio_in.data64 = nullptr;
    audio_in.channel_count = 2;
    audio_in.latency = 0;
    audio_in.constant_mask = 0;

    clap_audio_buffer audio_out{};
    audio_out.data32 = out_ptrs;
    audio_out.data64 = nullptr;
    audio_out.channel_count = 2;
    audio_out.latency = 0;
    audio_out.constant_mask = 0;

    clap_event_transport transport{};
    transport.header.size = sizeof(transport);
    transport.header.time = 0;
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.header.flags = 0;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo = 120.0;

    clap_input_events in_events{
        nullptr,
        [](const clap_input_events*) -> std::uint32_t { return 0U; },
        [](const clap_input_events*, std::uint32_t) -> const clap_event_header* { return nullptr; },
    };

    clap_output_events out_events{
        nullptr,
        [](const clap_output_events*, const clap_event_header*) -> bool { return true; },
    };

    clap_process proc{};
    proc.steady_time = -1;
    proc.frames_count = period_frames;
    proc.transport = &transport;
    proc.audio_inputs = &audio_in;
    proc.audio_inputs_count = 1;
    proc.audio_outputs = &audio_out;
    proc.audio_outputs_count = 1;
    proc.in_events = &in_events;
    proc.out_events = &out_events;

    flac_ax::audio::StereoFloatRing ring(period_frames * 64U);
    std::vector<float> interleaved(period_frames * 2U, 0.0f);

    const bool can_activate = (plugin->activate != nullptr);
    const bool can_start_proc = (plugin->start_processing != nullptr);
    const bool can_stop_proc = (plugin->stop_processing != nullptr);
    const bool can_deactivate = (plugin->deactivate != nullptr);

    if (can_activate && !plugin->activate(plugin, sample_rate, 1U, period_frames)) {
        std::fprintf(stderr, "CLAP plugin activate failed: %s\n", desc->id);
        plugin->destroy(plugin);
        entry->deinit();
        mod.close();
        return 1;
    }

    if (can_start_proc && !plugin->start_processing(plugin)) {
        std::fprintf(stderr, "CLAP plugin start_processing failed: %s\n", desc->id);
        if (can_deactivate) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        mod.close();
        return 1;
    }

    std::atomic<bool> sink_done{false};
    std::thread sink_thread([&]() {
        pin_thread_mask(0x01ULL, 80);
#if defined(AX_CLAP_HAS_ALSA)
        AlsaStereoSink alsa_sink;
        const bool alsa_ready = use_alsa && alsa_sink.Open(
            alsa_device.c_str(),
            static_cast<unsigned int>(sample_rate),
            period_frames);
#else
        (void)use_alsa;
        (void)alsa_device;
#endif
        std::vector<float> pop_float(period_frames * 2U, 0.0f);
        std::vector<std::int32_t> pop_s32(period_frames * 2U, 0);
        const auto pace_ns = std::chrono::nanoseconds(
            static_cast<std::uint64_t>((static_cast<double>(period_frames) * 1e9) / sample_rate));

        while (!sink_done.load(std::memory_order_acquire) || ring.AvailableToRead() > 0U) {
            const std::size_t popped = ring.PopInterleaved(pop_float.data(), period_frames);
            if (popped == 0U) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
#if defined(AX_CLAP_HAS_ALSA)
            if (alsa_ready) {
                flac_ax::audio::ConvertStereoFloatToS32(pop_float.data(), pop_s32.data(), popped);
                if (!alsa_sink.WriteFrames(pop_s32.data(), popped)) {
                    break;
                }
            } else {
                std::this_thread::sleep_for(pace_ns);
            }
#else
            (void)pop_s32;
            std::this_thread::sleep_for(pace_ns);
#endif
        }
#if defined(AX_CLAP_HAS_ALSA)
        alsa_sink.Close();
#endif
    });

    std::int64_t sample_clock = 0;
    while (g_running.load(std::memory_order_acquire) && plugin->process != nullptr) {
#if defined(AX_CLAP_HAS_TELEMETRY)
        const auto t0 = std::chrono::steady_clock::now();
#endif
        proc.steady_time = sample_clock;
        transport.header.time = static_cast<std::uint32_t>(sample_clock);
        (void)plugin->process(plugin, &proc);

        for (std::uint32_t n = 0; n < period_frames; ++n) {
            interleaved[n * 2U + 0U] = out_l[n];
            interleaved[n * 2U + 1U] = out_r[n];
        }

        std::size_t pushed = 0U;
        while (pushed < period_frames && g_running.load(std::memory_order_acquire)) {
            pushed += ring.PushInterleaved(interleaved.data() + pushed * 2U, period_frames - pushed);
            if (pushed < period_frames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

#if defined(AX_CLAP_HAS_TELEMETRY)
        const auto us = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count());
        ax::RecordAudioCallbackMetrics(us, period_frames);
#endif
        sample_clock += static_cast<std::int64_t>(period_frames);
    }

    sink_done.store(true, std::memory_order_release);
    if (sink_thread.joinable()) {
        sink_thread.join();
    }

    if (can_stop_proc) {
        plugin->stop_processing(plugin);
    }
    if (can_deactivate) {
        plugin->deactivate(plugin);
    }
    plugin->destroy(plugin);
    entry->deinit();
    mod.close();
    return 0;
}
