// C++20 CLAP single-plugin host wired to ax::audio::LockFreeStereoRing.
// Target: Piccolo-AX / i5-9600K (2:3:1 topology: DSP on Cores 2-4, DAC output on Core 0).

#include <clap/clap.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/file.h>
#include <unistd.h>
#else
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
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
#include <thread>
#include <vector>

namespace ax::audio {

class LockFreeStereoRing {
public:
    explicit LockFreeStereoRing(std::size_t capacity_frames)
        : capacity_(capacity_frames), buf_(capacity_frames * 2, 0.0f) {}

    std::size_t push_interleaved(const float* stereo_interleaved, std::size_t frames) noexcept {
        std::size_t w = write_idx_.load(std::memory_order_relaxed);
        const std::size_t r = read_idx_.load(std::memory_order_acquire);
        const std::size_t used = (w >= r) ? (w - r) : (capacity_ + w - r);
        const std::size_t free_frames = (capacity_ > 1 + used) ? (capacity_ - 1 - used) : 0;
        const std::size_t to_write = (frames < free_frames) ? frames : free_frames;

        for (std::size_t i = 0; i < to_write; ++i) {
            const std::size_t slot = ((w + i) % capacity_) * 2;
            buf_[slot]     = stereo_interleaved[i * 2];
            buf_[slot + 1] = stereo_interleaved[i * 2 + 1];
        }

        write_idx_.store((w + to_write) % capacity_, std::memory_order_release);
        return to_write;
    }

private:
    const std::size_t capacity_;
    std::vector<float> buf_;
    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::atomic<std::size_t> read_idx_{0};
};

} // namespace ax::audio

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) noexcept {
    g_running.store(false, std::memory_order_release);
}

void pin_dsp_thread_mask(std::uint64_t cpu_mask) noexcept {
#if !defined(_WIN32)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int cpu = 0; cpu < 64; ++cpu) {
        if ((cpu_mask >> cpu) & 1ULL) {
            CPU_SET(cpu, &cpuset);
        }
    }
    (void)::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)cpu_mask;
#endif
}

int acquire_dac_lock(const char* lock_path) noexcept {
#if !defined(_WIN32)
    const int fd = ::open(lock_path, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        return -1;
    }
    if (::flock(fd, LOCK_EX) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
#else
    (void)lock_path;
    return 0;
#endif
}

void release_dac_lock(int fd, const char* lock_path) noexcept {
#if !defined(_WIN32)
    if (fd >= 0) {
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }
    if (lock_path) {
        ::unlink(lock_path);
    }
#else
    (void)fd;
    (void)lock_path;
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

struct HostContext {
    const clap_host host{};
};

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <path-to-plugin.clap> [plugin-id] [sample-rate] [period-frames]\n", argv[0]);
        return 1;
    }

    const char* clap_path = argv[1];
    const char* requested_id = (argc >= 3 && argv[2][0] != '\0') ? argv[2] : nullptr;
    const double sample_rate = (argc >= 4) ? std::atof(argv[3]) : 48000.0;
    const std::uint32_t period_frames =
        (argc >= 5) ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 96U;

    if (sample_rate <= 0.0 || period_frames == 0U) {
        std::fprintf(stderr, "Invalid sample rate or period frames\n");
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    pin_dsp_thread_mask(0x1CULL);

    constexpr const char* kDacLockPath = "/run/lock/ax-dac.lock";
    const int dac_lock_fd = acquire_dac_lock(kDacLockPath);
    if (dac_lock_fd < 0) {
        std::fprintf(stderr, "Failed acquiring %s\n", kDacLockPath);
        return 1;
    }

    SharedModuleHandle mod;
    if (!mod.open(clap_path)) {
        std::fprintf(stderr, "Failed loading CLAP module: %s\n", clap_path);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const clap_plugin_entry* entry = mod.entry();
    if (!entry || !entry->init || !entry->deinit || !entry->get_factory) {
        std::fprintf(stderr, "Invalid clap_entry in %s\n", clap_path);
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (!entry->init(clap_path)) {
        std::fprintf(stderr, "clap_plugin_entry::init failed for %s\n", clap_path);
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const auto* factory =
        static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || !factory->get_plugin_count || !factory->get_plugin_descriptor || !factory->create_plugin) {
        std::fprintf(stderr, "CLAP plugin factory unavailable in %s\n", clap_path);
        entry->deinit();
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
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
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    static const clap_host_log host_log_ext{host_log};
    static HostContext host_ctx{
        clap_host{
            CLAP_VERSION,
            nullptr,
            "FLAC_AX",
            "The Silicon Preservation Society",
            "https://github.com/galaxywarriordad/FLAC_AX",
            "1.36.2",
            host_get_extension,
            host_request_restart,
            host_request_process,
            host_request_callback,
        }
    };
    (void)host_log_ext;

    const clap_plugin* plugin = factory->create_plugin(factory, &host_ctx.host, desc->id);
    if (!plugin || !plugin->init || !plugin->destroy) {
        std::fprintf(stderr, "Failed creating CLAP plugin instance: %s\n", desc->id);
        entry->deinit();
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (!plugin->init(plugin)) {
        std::fprintf(stderr, "CLAP plugin init failed: %s\n", desc->id);
        plugin->destroy(plugin);
        entry->deinit();
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
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

    ax::audio::LockFreeStereoRing ring(period_frames * 32U);
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
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (can_start_proc && !plugin->start_processing(plugin)) {
        std::fprintf(stderr, "CLAP plugin start_processing failed: %s\n", desc->id);
        if (can_deactivate) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        mod.close();
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const auto period_ns = std::chrono::nanoseconds(
        static_cast<std::uint64_t>((static_cast<double>(period_frames) * 1e9) / sample_rate));
    auto next_tick = std::chrono::steady_clock::now();
    std::int64_t sample_clock = 0;

    while (g_running.load(std::memory_order_acquire) && plugin->process != nullptr) {
        proc.steady_time = sample_clock;
        transport.header.time = static_cast<std::uint32_t>(sample_clock);
        (void)plugin->process(plugin, &proc);

        for (std::uint32_t n = 0; n < period_frames; ++n) {
            interleaved[n * 2U + 0U] = out_l[n];
            interleaved[n * 2U + 1U] = out_r[n];
        }
        ring.push_interleaved(interleaved.data(), period_frames);

        sample_clock += static_cast<std::int64_t>(period_frames);
        next_tick += period_ns;
        std::this_thread::sleep_until(next_tick);
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
    release_dac_lock(dac_lock_fd, kDacLockPath);
    return 0;
}
