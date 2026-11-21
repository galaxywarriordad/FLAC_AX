// src/main.cpp — Headless Bit-Perfect ESS Sabre FLAC & DoP (DSD64/DSD128) Player
#include "dsd_dop_reader.hpp"
#include "ess_dac_hooks.hpp"
#include "preload_policy.hpp"

#include <FLAC++/decoder.h>
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
#include <cstdio>
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
#define FLAC_AX_MAIN_HAVE_TELEMETRY 1
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
    sa.sa_flags = 0; // Do not SA_RESTART so SIGUSR1 / SIGINT / SIGTERM wake promptly
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

struct StreamMeta {
    std::atomic<unsigned int> sample_rate{48000U};
    std::atomic<unsigned int> channels{2U};
    std::atomic<unsigned int> bits_per_sample{24U};
    std::atomic<bool> is_dop{false};
    std::atomic<bool> format_changed{false};
};

// Lock-free SPSC stereo S32_LE frame ring buffer
class StereoFrameRing {
public:
    explicit StereoFrameRing(std::size_t capacity_frames = 262144U)
        : capacity_(capacity_frames), buf_(capacity_frames * 2U, 0) {}

    void Clear() noexcept {
        write_pos_.store(0U, std::memory_order_relaxed);
        read_pos_.store(0U, std::memory_order_release);
    }

    std::size_t WriteFrames(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        std::size_t w = write_pos_.load(std::memory_order_relaxed);
        const std::size_t r = read_pos_.load(std::memory_order_acquire);
        const std::size_t avail = (r + capacity_ - 1U - w) % capacity_;
        const std::size_t to_write = std::min(frames, avail);

        for (std::size_t i = 0; i < to_write; ++i) {
            const std::size_t idx = (w + i) % capacity_;
            buf_[idx * 2U + 0U] = interleaved_lr[i * 2U + 0U];
            buf_[idx * 2U + 1U] = interleaved_lr[i * 2U + 1U];
        }
        write_pos_.store((w + to_write) % capacity_, std::memory_order_release);
        return to_write;
    }

    std::size_t ReadFrames(std::int32_t* interleaved_lr, std::size_t max_frames) noexcept {
        const std::size_t w = write_pos_.load(std::memory_order_acquire);
        std::size_t r = read_pos_.load(std::memory_order_relaxed);
        const std::size_t avail = (w + capacity_ - r) % capacity_;
        const std::size_t to_read = std::min(max_frames, avail);

        for (std::size_t i = 0; i < to_read; ++i) {
            const std::size_t idx = (r + i) % capacity_;
            interleaved_lr[i * 2U + 0U] = buf_[idx * 2U + 0U];
            interleaved_lr[i * 2U + 1U] = buf_[idx * 2U + 1U];
        }
        read_pos_.store((r + to_read) % capacity_, std::memory_order_release);
        return to_read;
    }

private:
    const std::size_t capacity_;
    std::vector<std::int32_t> buf_;
    alignas(64) std::atomic<std::size_t> write_pos_{0U};
    alignas(64) std::atomic<std::size_t> read_pos_{0U};
};

inline std::int32_t PcmSampleToS32(FLAC__int32 sample, unsigned int bps) noexcept {
    if (bps >= 32U) {
        return static_cast<std::int32_t>(sample);
    }
    if (bps > 0U) {
        const unsigned int shift = 32U - bps;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(sample) << shift);
    }
    return 0;
}

// DsdTrackSession sits right ahead of PersistentDecoderWorker so Core 5 can stream .dsf / .dff DoP
struct DsdTrackSession {
    bool active{false};
    flac_ax::dsd::DsdStreamInfo info{};
    flac_ax::dsd::DsdDopStreamer streamer{};
    FILE* file_handle{nullptr};

    void Close() noexcept {
        active = false;
        info = {};
        if (file_handle != nullptr) {
            std::fclose(file_handle);
            file_handle = nullptr;
        }
    }

    ~DsdTrackSession() {
        Close();
    }

    bool TryOpenMemory(const std::uint8_t* data, std::uint64_t size) noexcept {
        Close();
        flac_ax::dsd::DsdStreamInfo parsed{};
        if (!flac_ax::dsd::ParseDsdMemoryHeader(data, size, parsed)) {
            return false;
        }
        info = parsed;
        streamer.Configure(info, data, size, nullptr);
        active = true;
        return true;
    }

    bool TryOpenFile(const std::string& path) {
        Close();
        flac_ax::dsd::DsdStreamInfo parsed{};
        if (!flac_ax::dsd::ParseDsdFileHeader(path, parsed)) {
            return false;
        }
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (fp == nullptr) {
            return false;
        }
        info = parsed;
        file_handle = fp;
        streamer.Configure(info, nullptr, 0U, file_handle);
        active = true;
        return true;
    }
};

class FlacRingDecoder final : public FLAC::Decoder::File {
public:
    FlacRingDecoder(StereoFrameRing& ring, StreamMeta& meta) noexcept
        : ring_(ring), meta_(meta) {}

    void metadata_callback(const ::FLAC__StreamMetadata* metadata) override {
        if (metadata != nullptr && metadata->type == ::FLAC__METADATA_TYPE_STREAMINFO) {
            const auto& si = metadata->data.stream_info;
            const unsigned int ch = (si.channels > 0U) ? si.channels : 2U;
            const unsigned int sr = (si.sample_rate > 0U) ? si.sample_rate : 48000U;
            const unsigned int bps = (si.bits_per_sample > 0U) ? si.bits_per_sample : 24U;

            meta_.channels.store(ch, std::memory_order_relaxed);
            meta_.bits_per_sample.store(bps, std::memory_order_relaxed);
            meta_.is_dop.store(false, std::memory_order_relaxed);
            if (meta_.sample_rate.exchange(sr, std::memory_order_acq_rel) != sr) {
                meta_.format_changed.store(true, std::memory_order_release);
            }
        }
    }

    ::FLAC__StreamDecoderWriteStatus write_callback(const ::FLAC__Frame* frame,
                                                    const FLAC__int32* const buffer[]) override {
        if (frame == nullptr || buffer == nullptr) {
            return ::FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        const unsigned int ch = (frame->header.channels > 0U) ? frame->header.channels : 2U;
        const unsigned int bps = (frame->header.bits_per_sample > 0U) ? frame->header.bits_per_sample : 24U;
        const std::size_t n_frames = static_cast<std::size_t>(frame->header.blocksize);

        if (interleaved_.size() < n_frames * 2U) {
            interleaved_.resize(n_frames * 2U, 0);
        }

        for (std::size_t i = 0; i < n_frames; ++i) {
            const FLAC__int32 l = buffer[0][i];
            const FLAC__int32 r = (ch >= 2U && buffer[1] != nullptr) ? buffer[1][i] : l;
            interleaved_[i * 2U + 0U] = PcmSampleToS32(l, bps);
            interleaved_[i * 2U + 1U] = PcmSampleToS32(r, bps);
        }

        std::size_t offset = 0U;
        while (offset < n_frames && g_running.load(std::memory_order_acquire)) {
            const std::size_t pushed =
                ring_.WriteFrames(interleaved_.data() + offset * 2U, n_frames - offset);
            offset += pushed;
            if (pushed == 0U) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        return g_running.load(std::memory_order_acquire)
                   ? ::FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE
                   : ::FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    void error_callback(::FLAC__StreamDecoderErrorStatus status) override {
        std::cerr << "[FLAC_AX] Decoder error: " << ::FLAC__StreamDecoderErrorStatusString[status] << "\n";
    }

private:
    StereoFrameRing& ring_;
    StreamMeta& meta_;
    std::vector<std::int32_t> interleaved_;
};

class PersistentDecoderWorker {
public:
    PersistentDecoderWorker(StereoFrameRing& ring,
                            StreamMeta& meta,
                            std::vector<std::string> tracks,
                            int pin_core = 5)
        : ring_(ring), meta_(meta), tracks_(std::move(tracks)), pin_core_(pin_core) {}

    void Start() {
        worker_ = std::thread([this]() { RunLoop(); });
    }

    void Join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void RunLoop() {
        PinThread(pin_core_, 0);
        flac_ax::preload::RamFilePreloader preloader;
        DsdTrackSession dsd_session;
        FlacRingDecoder flac_dec(ring_, meta_);
        std::vector<std::int32_t> dop_chunk(2048U * 2U, 0);

        for (const auto& path : tracks_) {
            if (!g_running.load(std::memory_order_acquire)) {
                break;
            }

            dsd_session.Close();
            const flac_ax::preload::PreloadedEntry* mem_entry = preloader.TryLoad(path);

            bool opened_dsd = false;
            if (mem_entry != nullptr && !mem_entry->bytes.empty()) {
                opened_dsd = dsd_session.TryOpenMemory(
                    mem_entry->bytes.data(), static_cast<std::uint64_t>(mem_entry->bytes.size()));
            }
            if (!opened_dsd) {
                opened_dsd = dsd_session.TryOpenFile(path);
            }

            if (opened_dsd && dsd_session.active) {
                meta_.channels.store(2U, std::memory_order_relaxed);
                meta_.bits_per_sample.store(24U, std::memory_order_relaxed);
                meta_.is_dop.store(true, std::memory_order_relaxed);
                if (meta_.sample_rate.exchange(dsd_session.info.dop_pcm_rate_hz,
                                               std::memory_order_acq_rel) != dsd_session.info.dop_pcm_rate_hz) {
                    meta_.format_changed.store(true, std::memory_order_release);
                }

                while (g_running.load(std::memory_order_acquire)) {
                    const std::size_t n_frames =
                        dsd_session.streamer.ReadDopFramesS32(dop_chunk.data(), 2048U);
                    if (n_frames == 0U) {
                        break;
                    }
                    std::size_t off = 0U;
                    while (off < n_frames && g_running.load(std::memory_order_acquire)) {
                        const std::size_t pushed =
                            ring_.WriteFrames(dop_chunk.data() + off * 2U, n_frames - off);
                        off += pushed;
                        if (pushed == 0U) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
                dsd_session.Close();
                continue;
            }

            if (flac_dec.init(path.c_str()) == ::FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                flac_dec.process_until_end_of_stream();
                flac_dec.finish();
            } else {
                std::cerr << "[FLAC_AX] Skipping unreadable track: " << path << "\n";
            }
        }
    }

    StereoFrameRing& ring_;
    StreamMeta& meta_;
    std::vector<std::string> tracks_;
    int pin_core_{5};
    std::thread worker_;
};

bool ConfigurePlayerAlsa(snd_pcm_t* pcm,
                         const flac_ax::ess::EssChipProfile& chip,
                         unsigned int target_rate_hz,
                         snd_pcm_uframes_t period_frames,
                         bool is_dop,
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

    const unsigned int mult = flac_ax::ess::RecommendedBufferPeriods(chip, rate, is_dop ? 1U : 0U);
    snd_pcm_uframes_t buffer = period * mult;
    if (snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer) < 0) return false;

    if (snd_pcm_hw_params(pcm, params) < 0) return false;
    if (snd_pcm_prepare(pcm) < 0) return false;

    out_rate = rate;
    out_period = period;
    return flac_ax::ess::OnEssHardwareConfigured(chip, pcm, out_rate, out_period);
}

void PrintUsage() {
    std::cerr << "Usage: flac_ax [options] <track1.flac|.dsf|.dff> [track2...]\n"
              << "  --device <alsa_dev>      ALSA PCM device (default: hw:CARD=FC3,DEV=0)\n"
              << "  --mixer-card <card>      ALSA mixer card for hw volume (default: FC3)\n"
              << "  --volume <0.0..1.0>      Set ESS hardware mixer playback level\n"
              << "  --rate <hz>              Initial target sample rate (default: 48000)\n"
              << "  --period <frames>        ALSA period frames (default: 256)\n";
}

} // namespace

int main(int argc, char** argv) {
    bool has_volume = false;
    double target_volume = 1.0;
    std::string device_override;
    std::string mixer_card = "FC3";
    unsigned int initial_rate = 48000U;
    snd_pcm_uframes_t period_frames = 256U;
    std::vector<std::string> tracks;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            device_override = argv[++i];
        } else if (arg == "--mixer-card" && i + 1 < argc) {
            mixer_card = argv[++i];
        } else if (arg == "--volume" && i + 1 < argc) {
            target_volume = std::stod(argv[++i]);
            has_volume = true;
        } else if (arg == "--rate" && i + 1 < argc) {
            initial_rate = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (arg == "--period" && i + 1 < argc) {
            period_frames = static_cast<snd_pcm_uframes_t>(std::stoul(argv[++i]));
        } else if (!arg.empty() && arg.front() == '-') {
            PrintUsage();
            return 1;
        } else {
            tracks.emplace_back(arg);
        }
    }

    if (tracks.empty()) {
        PrintUsage();
        return 1;
    }

    InstallSignals();
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[FLAC_AX] WARN: mlockall skipped (check CAP_IPC_LOCK / RLIMIT_MEMLOCK)\n";
    }

    const int lock_fd = ::open("/run/lock/ax-dac.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (lock_fd < 0) {
        std::cerr << "[FLAC_AX] Failed to open /run/lock/ax-dac.lock\n";
        return 1;
    }
    if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "[FLAC_AX] DAC lock busy (/run/lock/ax-dac.lock held by another process)\n";
        ::close(lock_fd);
        return 1;
    }

    const auto chip = flac_ax::ess::MakeEssProfile(flac_ax::ess::EssChipFamily::Es9281AcPro);
    const std::string alsa_dev = !device_override.empty() ? device_override : chip.default_alsa_device;

    flac_ax::ess::EssHardwareMixer hw_mixer;
    if (has_volume) {
        if (hw_mixer.Open(mixer_card.c_str()) && hw_mixer.SetNormalizedVolume(target_volume)) {
            std::cout << "[FLAC_AX] ESS hardware volume set on '" << mixer_card << "' to "
                      << std::clamp(target_volume, 0.0, 1.0) << "\n";
        } else {
            std::cerr << "[FLAC_AX] WARN: Failed to apply hardware volume on '" << mixer_card << "'\n";
        }
    }

#if defined(FLAC_AX_MAIN_HAVE_TELEMETRY)
    ax::EngineConfig ax_cfg{};
    (void)ax::LoadEngineConfig("ax_engine.ini", ax_cfg);
    if (ax_cfg.enable_shared_memory_export) {
        (void)ax::InitTelemetryShm();
    }
    ax::ResetTelemetry();
#endif

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, alsa_dev.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "[FLAC_AX] Failed to open ESS Sabre ALSA device '" << alsa_dev << "'\n";
#if defined(FLAC_AX_MAIN_HAVE_TELEMETRY)
        ax::ShutdownTelemetryShm();
#endif
        (void)::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return 1;
    }

    unsigned int actual_rate = initial_rate;
    snd_pcm_uframes_t actual_period = period_frames;
    if (!ConfigurePlayerAlsa(pcm, chip, initial_rate, period_frames, false, actual_rate, actual_period)) {
        std::cerr << "[FLAC_AX] Failed to configure ALSA hardware parameters\n";
        snd_pcm_close(pcm);
#if defined(FLAC_AX_MAIN_HAVE_TELEMETRY)
        ax::ShutdownTelemetryShm();
#endif
        (void)::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return 1;
    }

    StereoFrameRing ring(262144U);
    StreamMeta meta;
    meta.sample_rate.store(actual_rate, std::memory_order_relaxed);

    PersistentDecoderWorker decoder(ring, meta, std::move(tracks), 5);
    decoder.Start();

    PinThread(0, 80);
    std::vector<std::int32_t> out_buf(actual_period * 2U, 0);

    while (g_running.load(std::memory_order_acquire)) {
        if (meta.format_changed.exchange(false, std::memory_order_acq_rel)) {
            const unsigned int next_rate = meta.sample_rate.load(std::memory_order_acquire);
            const bool is_dop = meta.is_dop.load(std::memory_order_acquire);
            snd_pcm_drop(pcm);
            if (!ConfigurePlayerAlsa(pcm, chip, next_rate, period_frames, is_dop,
                                     actual_rate, actual_period)) {
                g_running.store(false, std::memory_order_release);
                break;
            }
            if (out_buf.size() < actual_period * 2U) {
                out_buf.resize(actual_period * 2U, 0);
            }
            std::cout << "[FLAC_AX] Reconfigured " << chip.chip_name << " -> "
                      << actual_rate << " Hz (" << (is_dop ? "DoP" : "PCM") << ")\n";
        }

        const std::size_t got = ring.ReadFrames(out_buf.data(), actual_period);
        if (got == 0U) {
            snd_pcm_wait(pcm, 2);
            continue;
        }

        std::size_t offset = 0U;
        while (offset < got && g_running.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, out_buf.data() + offset * 2U, got - offset);
            if (written >= 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written == -EAGAIN || written == -EINTR) {
                snd_pcm_wait(pcm, 2);
            } else if (written == -EPIPE) {
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

    g_running.store(false, std::memory_order_release);
    decoder.Join();

    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    hw_mixer.Close();

    (void)::flock(lock_fd, LOCK_UN);
    ::close(lock_fd);

#if defined(FLAC_AX_MAIN_HAVE_TELEMETRY)
    ax::ShutdownTelemetryShm();
#endif
    return 0;
}
