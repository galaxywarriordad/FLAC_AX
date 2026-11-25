// Minimal C++20 CLAP single-plugin host wired to ax::audio::LockFreeStereoRing.
// Target: Piccolo-AX / i5-9600K (2:3:1 topology: DSP on Cores 2-4, DAC output on Core 0).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <clap/clap.h>

// Replace with your ax-sdk ring header path when linking against libax_core, e.g.:
// #include "ax/audio/lockfree_audio_queue.hpp"
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
std::atomic<bool> g_restart_requested{false};
std::atomic<bool> g_callback_requested{false};
pthread_t g_main_thread{};

void handle_signal(int) noexcept {
    g_running.store(false, std::memory_order_release);
}

bool pin_current_thread_mask(uint64_t cpu_mask) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int cpu = 0; cpu < 64; ++cpu) {
        if ((cpu_mask >> cpu) & 1ULL) {
            CPU_SET(cpu, &cpuset);
        }
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

int acquire_dac_lock(const char* lock_path) noexcept {
    const int fd = ::open(lock_path, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        return -1;
    }
    if (::flock(fd, LOCK_EX) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void release_dac_lock(int fd, const char* lock_path) noexcept {
    if (fd >= 0) {
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }
    if (lock_path) {
        ::unlink(lock_path);
    }
}

// Pre-allocated flat CLAP event list (zero allocations on audio thread)
struct StaticInputEventList {
    static constexpr std::size_t kMaxEvents = 256;
    clap_event_midi_t events[kMaxEvents]{};
    uint32_t count = 0;

    void clear() noexcept { count = 0; }

    bool push_midi(uint32_t sample_offset, uint16_t port_index, uint8_t b0, uint8_t b1, uint8_t b2) noexcept {
        if (count >= kMaxEvents) return false;
        auto& ev = events[count++];
        ev.header.size = sizeof(clap_event_midi_t);
        ev.header.time = sample_offset;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.header.flags = 0;
        ev.port_index = port_index;
        ev.data[0] = b0;
        ev.data[1] = b1;
        ev.data[2] = b2;
        return true;
    }

    static uint32_t CLAP_ABI size_cb(const clap_input_events_t* list) noexcept {
        const auto* self = static_cast<const StaticInputEventList*>(list->ctx);
        return self ? self->count : 0;
    }

    static const clap_event_header_t* CLAP_ABI get_cb(const clap_input_events_t* list, uint32_t index) noexcept {
        const auto* self = static_cast<const StaticInputEventList*>(list->ctx);
        if (!self || index >= self->count) return nullptr;
        return &self->events[index].header;
    }
};

bool CLAP_ABI out_events_try_push(const clap_output_events_t*, const clap_event_header_t*) noexcept {
    // Discard plugin output events cleanly without blocking or allocating.
    return true;
}

const void* CLAP_ABI host_get_extension(const clap_host_t*, const char*) noexcept {
    return nullptr;
}

void CLAP_ABI host_request_restart(const clap_host_t*) noexcept {
    g_restart_requested.store(true, std::memory_order_release);
}

void CLAP_ABI host_request_process(const clap_host_t*) noexcept {
    // Host runs continuous deterministic block processing.
}

void CLAP_ABI host_request_callback(const clap_host_t*) noexcept {
    g_callback_requested.store(true, std::memory_order_release);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <plugin.clap> [plugin-id] [sample-rate] [period-frames]\n", argv[0]);
        return 1;
    }

    const char* clap_path = argv[1];
    const char* requested_id = (argc >= 3 && argv[2][0] != '\0' && std::strcmp(argv[2], "-") != 0) ? argv[2] : nullptr;
    const double sample_rate = (argc >= 4) ? std::atof(argv[3]) : 48000.0;
    const uint32_t period_frames = (argc >= 5) ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 96U;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    g_main_thread = pthread_self();

    // Pin host/DSP thread to Cores 2-4 (0x1C) on i5-9600K topology.
    pin_current_thread_mask(0x1CULL);

    constexpr const char* kDacLockPath = "/run/lock/ax-dac.lock";
    const int dac_lock_fd = acquire_dac_lock(kDacLockPath);
    if (dac_lock_fd < 0) {
        std::fprintf(stderr, "Failed acquiring %s\n", kDacLockPath);
        return 1;
    }

    void* dso = ::dlopen(clap_path, RTLD_LOCAL | RTLD_NOW);
    if (!dso) {
        std::fprintf(stderr, "dlopen failed for %s: %s\n", clap_path, ::dlerror());
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(::dlsym(dso, "clap_entry"));
    if (!entry || !clap_version_is_compatible(entry->clap_version)) {
        std::fprintf(stderr, "Invalid or incompatible clap_entry in %s\n", clap_path);
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (!entry->init(clap_path)) {
        std::fprintf(stderr, "clap_entry->init failed for %s\n", clap_path);
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) {
        std::fprintf(stderr, "CLAP plugin factory not found in %s\n", clap_path);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const uint32_t plugin_count = factory->get_plugin_count(factory);
    const clap_plugin_descriptor_t* chosen_desc = nullptr;
    for (uint32_t i = 0; i < plugin_count; ++i) {
        const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, i);
        if (!desc) continue;
        if (!requested_id || std::strcmp(desc->id, requested_id) == 0) {
            chosen_desc = desc;
            break;
        }
    }

    if (!chosen_desc) {
        std::fprintf(stderr, "No matching CLAP plugin descriptor found in %s\n", clap_path);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    clap_host_t host{};
    host.clap_version = CLAP_VERSION_INIT;
    host.host_data = nullptr;
    host.name = "ax-clap-host";
    host.vendor = "The Silicon Preservation Society";
    host.url = "https://github.com/galaxywarriordad/FLAC_AX";
    host.version = "1.0.0";
    host.get_extension = &host_get_extension;
    host.request_restart = &host_request_restart;
    host.request_process = &host_request_process;
    host.request_callback = &host_request_callback;

    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, chosen_desc->id);
    if (!plugin) {
        std::fprintf(stderr, "Failed creating CLAP plugin %s\n", chosen_desc->id);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (!plugin->init(plugin)) {
        std::fprintf(stderr, "plugin->init failed for %s\n", chosen_desc->id);
        plugin->destroy(plugin);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    if (!plugin->activate(plugin, sample_rate, period_frames, period_frames)) {
        std::fprintf(stderr, "plugin->activate failed for %s\n", chosen_desc->id);
        plugin->destroy(plugin);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    std::vector<float> in_l(period_frames, 0.0f);
    std::vector<float> in_r(period_frames, 0.0f);
    std::vector<float> out_l(period_frames, 0.0f);
    std::vector<float> out_r(period_frames, 0.0f);
    float* in_ptrs[2] = {in_l.data(), in_r.data()};
    float* out_ptrs[2] = {out_l.data(), out_r.data()};

    clap_audio_buffer_t in_buf{};
    in_buf.data32 = in_ptrs;
    in_buf.data64 = nullptr;
    in_buf.channel_count = 2;
    in_buf.latency = 0;
    in_buf.constant_mask = 0x3;

    clap_audio_buffer_t out_buf{};
    out_buf.data32 = out_ptrs;
    out_buf.data64 = nullptr;
    out_buf.channel_count = 2;
    out_buf.latency = 0;
    out_buf.constant_mask = 0;

    StaticInputEventList in_event_storage{};
    clap_input_events_t in_events{&in_event_storage, &StaticInputEventList::size_cb, &StaticInputEventList::get_cb};
    clap_output_events_t out_events{nullptr, &out_events_try_push};

    ax::audio::LockFreeStereoRing ring(period_frames * 32);
    std::vector<float> interleaved(period_frames * 2, 0.0f);

    if (!plugin->start_processing(plugin)) {
        std::fprintf(stderr, "plugin->start_processing failed for %s\n", chosen_desc->id);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        ::dlclose(dso);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const auto period_ns = std::chrono::nanoseconds(
        static_cast<uint64_t>((static_cast<double>(period_frames) * 1e9) / sample_rate));
    auto next_tick = std::chrono::steady_clock::now();
    int64_t steady_time = 0;

    // Hot DSP loop: zero heap allocations, zero blocking mutexes, zero I/O.
    while (g_running.load(std::memory_order_acquire)) {
        if (g_callback_requested.exchange(false, std::memory_order_acq_rel) && plugin->on_main_thread) {
            plugin->on_main_thread(plugin);
        }

        in_event_storage.clear();
        // Drain pre-queued RawMIDI events from Core 5 into `in_event_storage` here without allocating.

        std::memset(out_l.data(), 0, period_frames * sizeof(float));
        std::memset(out_r.data(), 0, period_frames * sizeof(float));

        clap_process_t proc{};
        proc.steady_time = steady_time;
        proc.frames_count = period_frames;
        proc.transport = nullptr;
        proc.audio_inputs = &in_buf;
        proc.audio_outputs = &out_buf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs_count = 1;
        proc.in_events = &in_events;
        proc.out_events = &out_events;

        const clap_process_status status = plugin->process(plugin, &proc);
        if (status == CLAP_PROCESS_ERROR) {
            break;
        }

        for (uint32_t n = 0; n < period_frames; ++n) {
            interleaved[n * 2]     = out_l[n];
            interleaved[n * 2 + 1] = out_r[n];
        }

        ring.push_interleaved(interleaved.data(), period_frames);
        steady_time += period_frames;

        next_tick += period_ns;
        std::this_thread::sleep_until(next_tick);
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    ::dlclose(dso);

    release_dac_lock(dac_lock_fd, kDacLockPath);
    return 0;
}
