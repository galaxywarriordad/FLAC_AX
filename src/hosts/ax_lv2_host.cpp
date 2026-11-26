// C++20 LV2 single-plugin host wired to StereoFloatRing, ScopedDacLock, and real sink drain.
// Target: Piccolo-AX / i5-9600K (2:3:1 topology: DSP on Cores 2-4, DAC output on Core 0).

#include "audio/ax_dac_lock.hpp"
#include "audio/ax_stereo_ring.hpp"

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#define AX_LV2_HAS_ALSA 1
#endif

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/telemetry.hpp>)
#include <ax/telemetry.hpp>
#define AX_LV2_HAS_TELEMETRY 1
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

#if defined(AX_LV2_HAS_ALSA)
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

private:
    snd_pcm_t* pcm_{nullptr};
    std::uint64_t xruns_{0ULL};
};
#endif

struct UridMap {
    LV2_URID midi_event = 0;
    LV2_URID atom_sequence = 0;

    static LV2_URID map_cb(LV2_URID_Map_Handle handle, const char* uri) {
        auto* self = static_cast<UridMap*>(handle);
        if (!self || !uri) return 0;
        if (std::strcmp(uri, LV2_MIDI__MidiEvent) == 0) return self->midi_event;
        if (std::strcmp(uri, LV2_ATOM__Sequence) == 0) return self->atom_sequence;
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
        std::fprintf(stderr, "Usage: %s <plugin-uri> [sample-rate] [period-frames] [--device <alsa_dev>] [--no-alsa]\n", argv[0]);
        return 1;
    }

    const char* plugin_uri_str = argv[1];
    const double sample_rate = (argc >= 3 && argv[2][0] != '-') ? std::atof(argv[2]) : 48000.0;
    const uint32_t period_frames =
        (argc >= 4 && argv[3][0] != '-') ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 96U;
    std::string alsa_device = "hw:CARD=FC3,DEV=0";
    bool use_alsa = true;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            alsa_device = argv[++i];
        } else if (arg == "--no-alsa") {
            use_alsa = false;
        }
    }

    if (sample_rate <= 0.0 || period_frames == 0U) {
        std::fprintf(stderr, "Invalid sample rate or period frames\n");
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
#if defined(SIGUSR1)
    std::signal(SIGUSR1, handle_signal);
#endif

    pin_thread_mask(0x1CULL, 0);

    LilvWorld* world = lilv_world_new();
    lilv_world_load_all(world);

    LilvNode* plugin_uri = lilv_new_uri(world, plugin_uri_str);
    const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
    if (!plugin) {
        std::fprintf(stderr, "LV2 plugin not found: %s\n", plugin_uri_str);
        lilv_node_free(plugin_uri);
        lilv_world_free(world);
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
        return 1;
    }

    flac_ax::audio::ScopedDacLock dac_lock;
    if (!dac_lock.Acquire("/run/lock/ax-dac.lock", false)) {
        std::fprintf(stderr, "Failed acquiring /run/lock/ax-dac.lock (err=%d)\n", dac_lock.last_error());
        lilv_instance_free(instance);
        lilv_node_free(plugin_uri);
        lilv_world_free(world);
        return 1;
    }

    LilvNode* audio_class = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode* control_class = lilv_new_uri(world, LV2_CORE__ControlPort);
    LilvNode* input_class = lilv_new_uri(world, LV2_CORE__InputPort);
    LilvNode* output_class = lilv_new_uri(world, LV2_CORE__OutputPort);
    LilvNode* atom_class = lilv_new_uri(world, LV2_ATOM__AtomPort);

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

    flac_ax::audio::StereoFloatRing ring(period_frames * 64U);
    ring.PrefaultStorage();
    std::vector<float> interleaved(period_frames * 2U, 0.0f);

    lilv_instance_activate(instance);

    std::atomic<bool> sink_done{false};
    std::thread sink_thread([&]() {
        pin_thread_mask(0x01ULL, 80);
#if defined(AX_LV2_HAS_ALSA)
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
        const auto short_wait_ns = std::chrono::nanoseconds(
            std::max<std::uint64_t>(50000ULL, static_cast<std::uint64_t>(pace_ns.count()) / 4ULL));

        while (!sink_done.load(std::memory_order_acquire) || ring.AvailableToRead() > 0U) {
            const std::size_t popped = ring.PopInterleaved(pop_float.data(), period_frames);
            if (popped == 0U) {
                std::this_thread::sleep_for(short_wait_ns);
                continue;
            }
#if defined(AX_LV2_HAS_ALSA)
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
#if defined(AX_LV2_HAS_ALSA)
        alsa_sink.Close();
#endif
    });

    const auto pace_ns = std::chrono::nanoseconds(
        static_cast<std::uint64_t>((static_cast<double>(period_frames) * 1e9) / sample_rate));
    const auto full_wait_ns = std::chrono::nanoseconds(
        std::max<std::uint64_t>(50000ULL, static_cast<std::uint64_t>(pace_ns.count()) / 2ULL));

    while (g_running.load(std::memory_order_acquire)) {
#if defined(AX_LV2_HAS_TELEMETRY)
        const auto t0 = std::chrono::steady_clock::now();
#endif
        if (ports.midi_in_port >= 0) {
            auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(ports.midi_in_buf.data());
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        }

        lilv_instance_run(instance, period_frames);

        for (uint32_t n = 0; n < period_frames; ++n) {
            const float l = ports.audio_out_l[n];
            const float r = (ports.out_r_port >= 0) ? ports.audio_out_r[n] : l;
            interleaved[n * 2U + 0U] = l;
            interleaved[n * 2U + 1U] = r;
        }

        std::size_t pushed = 0U;
        while (pushed < period_frames && g_running.load(std::memory_order_acquire)) {
            pushed += ring.PushInterleaved(interleaved.data() + pushed * 2U, period_frames - pushed);
            if (pushed < period_frames) {
                std::this_thread::sleep_for(full_wait_ns);
            }
        }

#if defined(AX_LV2_HAS_TELEMETRY)
        const auto us = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count());
        ax::RecordAudioCallbackMetrics(us, period_frames);
#endif
    }

    sink_done.store(true, std::memory_order_release);
    if (sink_thread.joinable()) {
        sink_thread.join();
    }

    dac_lock.Release();
    lilv_instance_deactivate(instance);
    lilv_instance_free(instance);

    lilv_node_free(audio_class);
    lilv_node_free(control_class);
    lilv_node_free(input_class);
    lilv_node_free(output_class);
    lilv_node_free(atom_class);
    lilv_node_free(plugin_uri);
    lilv_world_free(world);
    return 0;
}
