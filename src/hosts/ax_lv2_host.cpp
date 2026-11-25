// Minimal C++20 LV2 single-plugin host wired to ax::audio::LockFreeStereoRing.
// Target: Piccolo-AX / i5-9600K (2:3:1 topology: DSP on Cores 2-4, DAC output on Core 0).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

// Replace with your ax-sdk ring header path, e.g.:
// #include "ax/audio/lockfree_audio_queue.hpp"
namespace ax::audio {

// Minimal placeholder signature matching LockFreeStereoRing conventions;
// bind directly to ax::audio::LockFreeStereoRing from ax-sdk.
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

struct UridMap {
    LV2_URID midi_event = 0;
    LV2_URID atom_sequence = 0;

    static LV2_URID map_cb(LV2_URID_Map_Handle handle, const char* uri) {
        auto* self = static_cast<UridMap*>(handle);
        if (!self || !uri) return 0;
        if (std::strcmp(uri, LV2_MIDI__MidiEvent) == 0) return self->midi_event;
        if (std::strcmp(uri, LV2_ATOM__Sequence) == 0) return self->atom_sequence;
        // Extend with a pre-allocated table if needed; avoid dynamic allocation in RT code.
        return 0;
    }
};

struct PortBindings {
    uint32_t num_ports = 0;
    std::vector<float> audio_in_l;
    std::vector<float> audio_in_r;
    std::vector<float> audio_out_l;
    std::vector<float> audio_out_r;
    std::vector<float> control_vals;
    std::vector<uint8_t> midi_in_buf;
    std::vector<uint8_t> atom_out_buf;

    int in_l_port = -1;
    int in_r_port = -1;
    int out_l_port = -1;
    int out_r_port = -1;
    int midi_in_port = -1;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <plugin-uri> [sample-rate] [period-frames]\n", argv[0]);
        return 1;
    }

    const char* plugin_uri_str = argv[1];
    const double sample_rate = (argc >= 3) ? std::atof(argv[2]) : 48000.0;
    const uint32_t period_frames = (argc >= 4) ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 96U;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Pin host/DSP thread to Cores 2-4 (0x1C) on i5-9600K topology.
    pin_current_thread_mask(0x1CULL);

    constexpr const char* kDacLockPath = "/run/lock/ax-dac.lock";
    const int dac_lock_fd = acquire_dac_lock(kDacLockPath);
    if (dac_lock_fd < 0) {
        std::fprintf(stderr, "Failed acquiring %s\n", kDacLockPath);
        return 1;
    }

    LilvWorld* world = lilv_world_new();
    lilv_world_load_all(world);

    LilvNode* plugin_uri = lilv_new_uri(world, plugin_uri_str);
    const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
    if (!plugin) {
        std::fprintf(stderr, "LV2 plugin not found: %s\n", plugin_uri_str);
        lilv_node_free(plugin_uri);
        lilv_world_free(world);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    UridMap urid_map{};
    urid_map.midi_event = 1;
    urid_map.atom_sequence = 2;

    LV2_URID_Map map_feature_data{&urid_map, &UridMap::map_cb};
    const LV2_Feature map_feature{LV2_URID__map, &map_feature_data};
    const LV2_Feature* features[] = {&map_feature, nullptr};

    LilvInstance* instance = lilv_plugin_instantiate(plugin, sample_rate, features);
    if (!instance) {
        std::fprintf(stderr, "Failed instantiating LV2 plugin: %s\n", plugin_uri_str);
        lilv_node_free(plugin_uri);
        lilv_world_free(world);
        release_dac_lock(dac_lock_fd, kDacLockPath);
        return 1;
    }

    const LilvNode* audio_class = lilv_new_uri(world, LV2_CORE__AudioPort);
    const LilvNode* control_class = lilv_new_uri(world, LV2_CORE__ControlPort);
    const LilvNode* input_class = lilv_new_uri(world, LV2_CORE__InputPort);
    const LilvNode* output_class = lilv_new_uri(world, LV2_CORE__OutputPort);
    const LilvNode* atom_class = lilv_new_uri(world, LV2_ATOM__AtomPort);

    PortBindings ports{};
    ports.num_ports = lilv_plugin_get_num_ports(plugin);
    ports.audio_in_l.assign(period_frames, 0.0f);
    ports.audio_in_r.assign(period_frames, 0.0f);
    ports.audio_out_l.assign(period_frames, 0.0f);
    ports.audio_out_r.assign(period_frames, 0.0f);
    ports.control_vals.assign(ports.num_ports, 0.0f);
    ports.midi_in_buf.assign(4096, 0);
    ports.atom_out_buf.assign(4096, 0);

    for (uint32_t i = 0; i < ports.num_ports; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
        const bool is_input = lilv_port_is_a(plugin, port, input_class);
        const bool is_output = lilv_port_is_a(plugin, port, output_class);

        if (lilv_port_is_a(plugin, port, audio_class)) {
            if (is_input) {
                if (ports.in_l_port < 0) ports.in_l_port = static_cast<int>(i);
                else if (ports.in_r_port < 0) ports.in_r_port = static_cast<int>(i);
                lilv_instance_connect_port(instance, i, (ports.in_r_port == static_cast<int>(i)) ? ports.audio_in_r.data() : ports.audio_in_l.data());
            } else if (is_output) {
                if (ports.out_l_port < 0) ports.out_l_port = static_cast<int>(i);
                else if (ports.out_r_port < 0) ports.out_r_port = static_cast<int>(i);
                lilv_instance_connect_port(instance, i, (ports.out_r_port == static_cast<int>(i)) ? ports.audio_out_r.data() : ports.audio_out_l.data());
            }
        } else if (lilv_port_is_a(plugin, port, control_class)) {
            if (is_input) {
                float def = 0.0f, min_v = 0.0f, max_v = 1.0f;
                LilvNode* def_node = nullptr;
                LilvNode* min_node = nullptr;
                LilvNode* max_node = nullptr;
                lilv_port_get_range(plugin, port, &def_node, &min_node, &max_node);
                if (def_node && lilv_node_is_float(def_node)) def = lilv_node_as_float(def_node);
                if (min_node && lilv_node_is_float(min_node)) min_v = lilv_node_as_float(min_node);
                if (max_node && lilv_node_is_float(max_node)) max_v = lilv_node_as_float(max_node);
                ports.control_vals[i] = (def < min_v) ? min_v : ((def > max_v) ? max_v : def);
                lilv_node_free(def_node);
                lilv_node_free(min_node);
                lilv_node_free(max_node);
            }
            lilv_instance_connect_port(instance, i, &ports.control_vals[i]);
        } else if (lilv_port_is_a(plugin, port, atom_class)) {
            if (is_input && ports.midi_in_port < 0) {
                ports.midi_in_port = static_cast<int>(i);
                auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(ports.midi_in_buf.data());
                seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
                seq->atom.type = urid_map.atom_sequence;
                seq->body.unit = 0;
                seq->body.pad = 0;
                lilv_instance_connect_port(instance, i, seq);
            } else if (is_output) {
                lilv_instance_connect_port(instance, i, ports.atom_out_buf.data());
            }
        }
    }

    ax::audio::LockFreeStereoRing ring(period_frames * 32);
    std::vector<float> interleaved(period_frames * 2, 0.0f);

    lilv_instance_activate(instance);

    const auto period_ns = std::chrono::nanoseconds(
        static_cast<uint64_t>((static_cast<double>(period_frames) * 1e9) / sample_rate));
    auto next_tick = std::chrono::steady_clock::now();

    // Hot DSP loop: zero heap allocations, zero blocking mutexes, zero I/O.
    while (g_running.load(std::memory_order_acquire)) {
        if (ports.midi_in_port >= 0) {
            auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(ports.midi_in_buf.data());
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
            // Drain pre-queued MIDI events from Core 5 into `seq` here without allocating.
        }

        lilv_instance_run(instance, period_frames);

        for (uint32_t n = 0; n < period_frames; ++n) {
            const float l = ports.audio_out_l[n];
            const float r = (ports.out_r_port >= 0) ? ports.audio_out_r[n] : l;
            interleaved[n * 2]     = l;
            interleaved[n * 2 + 1] = r;
        }

        ring.push_interleaved(interleaved.data(), period_frames);

        next_tick += period_ns;
        std::this_thread::sleep_until(next_tick);
    }

    lilv_instance_deactivate(instance);
    lilv_instance_free(instance);

    lilv_node_free(audio_class);
    lilv_node_free(control_class);
    lilv_node_free(input_class);
    lilv_node_free(output_class);
    lilv_node_free(atom_class);
    lilv_node_free(plugin_uri);
    lilv_world_free(world);

    release_dac_lock(dac_lock_fd, kDacLockPath);
    return 0;
}
