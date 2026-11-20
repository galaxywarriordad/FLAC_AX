// src/main.cpp — Bare-metal Linux FLAC -> ALSA engine with ESS DAC hooks,
// zero-disk FLAC::Decoder::Stream playback, RT memory locking, and rate-lock
#include "ess_dac_hooks.hpp"
#include "preload_policy.hpp"

#include <FLAC++/decoder.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/audio.hpp>)
#include <ax/audio.hpp>
#define FLAC_AX_HAVE_AX_AUDIO_HPP 1
#endif
#if __has_include(<ax/config.hpp>) && __has_include(<ax/telemetry.hpp>)
#include <ax/config.hpp>
#include <ax/telemetry.hpp>
#define FLAC_AX_HAVE_AX_TELEMETRY_HPP 1
#endif
#endif

namespace {

constexpr std::size_t kRingCapacityFrames = 65536; // Must remain a power of 2
constexpr std::size_t kStartupWatermarkFrames = 16384; // ~25% of ring (~341 ms @ 48 kHz)
constexpr std::size_t kXrunRecoveryFrames = 4096;      // Safe refill floor after EPIPE

static_assert((kRingCapacityFrames & (kRingCapacityFrames - 1)) == 0,
              "kRingCapacityFrames must be a power of two");

std::atomic<bool> g_stop_requested{false};

void HandleSignal(int /*sig*/) noexcept {
    g_stop_requested.store(true, std::memory_order_release);
}

void InstallSignalHandlers() noexcept {
    struct sigaction sa {};
    sa.sa_handler = HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Do not SA_RESTART so blocking ALSA calls wake promptly on SIGINT/SIGTERM
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

template <std::size_t CapacityFrames>
class StereoFrameRing {
    static_assert((CapacityFrames & (CapacityFrames - 1)) == 0,
                  "CapacityFrames must be a power of two");
    static constexpr std::size_t kMask = CapacityFrames - 1;

public:
    void Reset() noexcept {
#if !defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
#endif
    }

#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        return ax_ring_.PushBatch(interleaved_lr, frames);
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        return ax_ring_.PopBatch(out_interleaved_lr, max_frames);
    }

    std::size_t Size() const noexcept {
        return ax_ring_.Size();
    }
#else
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t used = (head - tail) & kMask;
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

        head_.store((head + frames) & kMask, std::memory_order_release);
        return true;
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t available = (head - tail) & kMask;
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

        tail_.store((tail + to_pop) & kMask, std::memory_order_release);
        return to_pop;
    }

    std::size_t Size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }
#endif

private:
#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    ax::audio::LockFreeStereoRing<CapacityFrames, std::int32_t> ax_ring_{};
#else
    alignas(64) std::array<std::array<std::int32_t, 2>, CapacityFrames> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    char pad_head_[64 - sizeof(std::atomic<std::size_t>)]{};
    alignas(64) std::atomic<std::size_t> tail_{0};
    char pad_tail_[64 - sizeof(std::atomic<std::size_t>)]{};
#endif
};

struct StreamMeta {
    std::atomic<unsigned int> sample_rate{0};
    std::atomic<unsigned int> bits_per_sample{0};
    std::atomic<unsigned int> channels{0};
    std::atomic<bool> meta_ready{false};
    std::atomic<bool> eof_or_error{false};

    void Reset() noexcept {
        sample_rate.store(0, std::memory_order_relaxed);
        bits_per_sample.store(0, std::memory_order_relaxed);
        channels.store(0, std::memory_order_relaxed);
        meta_ready.store(false, std::memory_order_relaxed);
        eof_or_error.store(false, std::memory_order_relaxed);
    }
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

// Decodes directly from preloaded RAM bytes when available, or falls back to disk streaming.
class FlacMemoryOrFileDecoder final : public FLAC::Decoder::File {
public:
    FlacMemoryOrFileDecoder(StereoFrameRing<kRingCapacityFrames>& ring,
                            StreamMeta& meta,
                            std::vector<std::int32_t>& shared_scratch)
        : ring_(ring), meta_(meta), pcm_scratch_(shared_scratch) {}

    ::FLAC__StreamDecoderInitStatus InitStream(const std::string& fallback_path,
                                              const std::vector<std::uint8_t>* preloaded_bytes,
                                              const std::atomic<bool>& cancel_track) {
        mem_bytes_ = preloaded_bytes;
        cancel_track_ = &cancel_track;
        if (mem_bytes_ != nullptr && !mem_bytes_->empty()) {
            mem_offset_ = 0;
            return FLAC::Decoder::Stream::init();
        }
        return FLAC::Decoder::File::init(fallback_path);
    }

protected:
    ::FLAC__StreamDecoderReadStatus read_callback(FLAC__byte buffer[], std::size_t* bytes) override {
        if (mem_bytes_ == nullptr) {
            return FLAC::Decoder::File::read_callback(buffer, bytes);
        }
        if (*bytes == 0) {
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        if (mem_offset_ >= mem_bytes_->size()) {
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
        const std::size_t remaining = mem_bytes_->size() - mem_offset_;
        const std::size_t to_copy = std::min(*bytes, remaining);
        std::memcpy(buffer, mem_bytes_->data() + mem_offset_, to_copy);
        mem_offset_ += to_copy;
        *bytes = to_copy;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    ::FLAC__StreamDecoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset) override {
        if (mem_bytes_ == nullptr) {
            return FLAC::Decoder::File::seek_callback(absolute_byte_offset);
        }
        if (absolute_byte_offset > mem_bytes_->size()) {
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        }
        mem_offset_ = static_cast<std::size_t>(absolute_byte_offset);
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }

    ::FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64* absolute_byte_offset) override {
        if (mem_bytes_ == nullptr) {
            return FLAC::Decoder::File::tell_callback(absolute_byte_offset);
        }
        *absolute_byte_offset = static_cast<FLAC__uint64>(mem_offset_);
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }

    ::FLAC__StreamDecoderLengthStatus length_callback(FLAC__uint64* stream_length) override {
        if (mem_bytes_ == nullptr) {
            return FLAC::Decoder::File::length_callback(stream_length);
        }
        *stream_length = static_cast<FLAC__uint64>(mem_bytes_->size());
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }

    bool eof_callback() override {
        if (mem_bytes_ == nullptr) {
            return FLAC::Decoder::File::eof_callback();
        }
        return mem_offset_ >= mem_bytes_->size();
    }

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

        const FLAC__int32* const ch0 = buffer[0];
        if (channels >= 2U) {
            const FLAC__int32* const ch1 = buffer[1];
            for (std::size_t i = 0; i < blocksize; ++i) {
                pcm_scratch_[i * 2 + 0] = SampleToS32(ch0[i], bps);
                pcm_scratch_[i * 2 + 1] = SampleToS32(ch1[i], bps);
            }
        } else {
            for (std::size_t i = 0; i < blocksize; ++i) {
                const std::int32_t s = SampleToS32(ch0[i], bps);
                pcm_scratch_[i * 2 + 0] = s;
                pcm_scratch_[i * 2 + 1] = s;
            }
        }

        while (!g_stop_requested.load(std::memory_order_acquire) &&
               !(cancel_track_ && cancel_track_->load(std::memory_order_acquire))) {
            if (ring_.Push(pcm_scratch_.data(), blocksize)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }

        const bool aborted = g_stop_requested.load(std::memory_order_acquire) ||
                             (cancel_track_ && cancel_track_->load(std::memory_order_acquire));
        return aborted ? FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
                       : FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void error_callback(::FLAC__StreamDecoderErrorStatus status) override {
        std::cerr << "[FLAC] Decoder error: " << ::FLAC__StreamDecoderErrorStatusString[status] << "\n";
        meta_.eof_or_error.store(true, std::memory_order_release);
    }

private:
    StereoFrameRing<kRingCapacityFrames>& ring_;
    StreamMeta& meta_;
    std::vector<std::int32_t>& pcm_scratch_;
    const std::vector<std::uint8_t>* mem_bytes_{nullptr};
    const std::atomic<bool>* cancel_track_{nullptr};
    std::size_t mem_offset_{0};
};

// Persistent non-RT decoder worker pinned to Core 5 (storage_aux) to eliminate per-track thread churn.
class PersistentDecoderWorker {
public:
    PersistentDecoderWorker(StereoFrameRing<kRingCapacityFrames>& ring, StreamMeta& meta, int core_id)
        : ring_(ring), meta_(meta), core_id_(core_id) {
        pcm_scratch_.reserve(16384 * 2); // Pre-reserve max typical FLAC blocksize (16384 stereo frames)
        worker_ = std::thread(&PersistentDecoderWorker::RunLoop, this);
    }

    ~PersistentDecoderWorker() {
        Shutdown();
    }

    void StartTrack(const std::string& path, const std::vector<std::uint8_t>* preloaded_bytes) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            current_path_ = path;
            current_bytes_ = preloaded_bytes;
            cancel_track_.store(false, std::memory_order_relaxed);
            decode_finished_.store(false, std::memory_order_relaxed);
            has_job_ = true;
        }
        cv_.notify_one();
    }

    void CancelCurrentTrack() noexcept {
        cancel_track_.store(true, std::memory_order_release);
    }

    void WaitTrackFinished() {
        std::unique_lock<std::mutex> lock(mtx_);
        done_cv_.wait(lock, [this]() { return !has_job_; });
    }

    bool IsFinished() const noexcept {
        return decode_finished_.load(std::memory_order_acquire);
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (shutdown_) {
                return;
            }
            shutdown_ = true;
            cancel_track_.store(true, std::memory_order_release);
        }
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void RunLoop() {
        if (core_id_ >= 0) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(static_cast<unsigned int>(core_id_), &set);
            pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
        }

        while (true) {
            std::string path;
            const std::vector<std::uint8_t>* bytes = nullptr;

            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this]() { return has_job_ || shutdown_; });
                if (shutdown_) {
                    return;
                }
                path = current_path_;
                bytes = current_bytes_;
            }

            FlacMemoryOrFileDecoder decoder(ring_, meta_, pcm_scratch_);
            const FLAC__StreamDecoderInitStatus init_status =
                decoder.InitStream(path, bytes, cancel_track_);
            if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                std::cerr << "[FLAC] Failed to initialize decoder for '" << path << "' (status="
                          << FLAC__StreamDecoderInitStatusString[init_status] << ")\n";
                meta_.eof_or_error.store(true, std::memory_order_release);
            } else {
                decoder.process_until_end_of_stream();
                decoder.finish();
            }

            decode_finished_.store(true, std::memory_order_release);

            {
                std::lock_guard<std::mutex> lock(mtx_);
                has_job_ = false;
            }
            done_cv_.notify_all();
        }
    }

    StereoFrameRing<kRingCapacityFrames>& ring_;
    StreamMeta& meta_;
    int core_id_;
    std::vector<std::int32_t> pcm_scratch_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    bool has_job_{false};
    bool shutdown_{false};
    std::string current_path_;
    const std::vector<std::uint8_t>* current_bytes_{nullptr};
    std::atomic<bool> cancel_track_{false};
    std::atomic<bool> decode_finished_{false};
    std::thread worker_;
};

bool ApplyThreadAffinityAndPriority(int cpu_id, int sched_fifo_prio, bool warn_on_rt_fail = true) noexcept {
    if (cpu_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(cpu_id), &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
            std::cerr << "[WARN] Failed to set CPU affinity to core " << cpu_id << "\n";
        }
    }

    sched_param param{};
    if (sched_fifo_prio > 0) {
        param.sched_priority = sched_fifo_prio;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            if (warn_on_rt_fail) {
                std::cerr << "[WARN] Failed to set SCHED_FIFO priority " << sched_fifo_prio
                          << " (check CAP_SYS_NICE / ulimit)\n";
            }
            return false;
        }
        return true;
    }

    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
    return true;
}

void LockProcessMemory() noexcept {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[WARN] mlockall(MCL_CURRENT|MCL_FUTURE) skipped (check CAP_IPC_LOCK / RLIMIT_MEMLOCK)\n";
    }
}

bool ConfigureAlsaForEss(snd_pcm_t* pcm,
                         const flac_ax::ess::EssChipProfile& chip,
                         unsigned int stream_rate_hz,
                         snd_pcm_uframes_t period_frames,
                         unsigned int buffer_periods_override,
                         unsigned int& out_actual_rate,
                         snd_pcm_uframes_t& out_actual_period) {
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);

    int err = snd_pcm_hw_params_any(pcm, params);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_any failed: " << snd_strerror(err) << "\n";
        return false;
    }

    // Enforce bit-perfect hardware clocking (never engage ALSA software resampling)
    err = snd_pcm_hw_params_set_rate_resample(pcm, params, 0);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_rate_resample(0) failed: " << snd_strerror(err) << "\n";
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

    const unsigned int mult =
        flac_ax::ess::RecommendedBufferPeriods(chip, rate, buffer_periods_override);
    snd_pcm_uframes_t buffer = period * mult;
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

    const bool exact_lock = (out_actual_rate == stream_rate_hz);
    std::cout << "[ESS] Chip=" << chip.chip_name
              << " rate=" << out_actual_rate << " Hz (stream=" << stream_rate_hz << " Hz, "
              << (exact_lock ? "BIT-PERFECT LOCK" : "RATE-ADJUSTED") << ")"
              << ", period=" << out_actual_period
              << " frames, buffer=" << buffer << " frames (" << mult << "x)\n";
    return true;
}

void WaitForStartupWatermark(const StereoFrameRing<kRingCapacityFrames>& ring,
                             const PersistentDecoderWorker& decoder_worker,
                             const StreamMeta& meta,
                             std::size_t target_frames) noexcept {
    while (!g_stop_requested.load(std::memory_order_acquire) &&
           !meta.eof_or_error.load(std::memory_order_acquire) &&
           !decoder_worker.IsFinished()) {
        if (ring.Size() >= target_frames) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
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
    std::cerr << "Usage: flac_ax <file1.flac> [file2.flac ...] [options]\n"
              << "  --ess <es9281ac_pro|es9281pro>   Target ESS DAC chip hook (default: es9281ac_pro)\n"
              << "  --device <alsa_dev>              ALSA PCM device (default: chip default)\n"
              << "  --period <frames>                ALSA period frames (default: chip default 96)\n"
              << "  --buffer-periods <count>         ALSA buffer multiplier (default: auto 4x/8x)\n"
              << "  --profiles <path/to/json>        Cross-check ax-sdk hardware_profiles.json\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::vector<std::string> queue;
    std::string ess_arg = "es9281ac_pro";
    std::string device_override;
    std::string profiles_arg;
    bool has_period_override = false;
    snd_pcm_uframes_t period_frames = 96U;
    unsigned int buffer_periods_override = 0U;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--ess" && i + 1 < argc) {
            ess_arg = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            device_override = argv[++i];
        } else if (arg == "--period" && i + 1 < argc) {
            period_frames = static_cast<snd_pcm_uframes_t>(std::stoul(argv[++i]));
            has_period_override = true;
        } else if (arg == "--buffer-periods" && i + 1 < argc) {
            buffer_periods_override = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (arg == "--profiles" && i + 1 < argc) {
            profiles_arg = argv[++i];
        } else if (!arg.empty() && arg.front() != '-') {
            queue.emplace_back(arg);
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (queue.empty()) {
        PrintUsage();
        return 1;
    }

    InstallSignalHandlers();
    LockProcessMemory();

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

#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
    ax::EngineConfig ax_cfg{};
    if (ax::LoadEngineConfig("ax_engine.ini", ax_cfg)) {
        if (!has_period_override && ax_cfg.alsa_period_frames > 0U) {
            period_frames = static_cast<snd_pcm_uframes_t>(ax_cfg.alsa_period_frames);
        }
    }
    if (ax_cfg.enable_shared_memory_export) {
        if (ax::InitTelemetryShm()) {
            std::cout << "[AX-SDK] Shared-memory telemetry active (/dev/shm/ax_telemetry)\n";
        }
    }
    ax::ResetTelemetry();
#endif

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
    flac_ax::RamFilePreloader preloader(preload_policy);
    std::cout << "[PRELOAD] MemTotal=" << (preload_policy.detected_mem_total_bytes / (1024ULL * 1024ULL))
              << " MiB, mode="
              << (preload_policy.mode == flac_ax::PreloadMode::WholeAlbum ? "WholeAlbum" :
                  (preload_policy.mode == flac_ax::PreloadMode::Window3Files ? "Window3Files" : "SingleFile"))
              << ", tracks=" << queue.size() << "\n";

    snd_pcm_t* pcm = nullptr;
    int open_err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (open_err < 0) {
        std::cerr << "[ALSA] Failed to open '" << device << "': " << snd_strerror(open_err) << "\n";
#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
        ax::ShutdownTelemetryShm();
#endif
        return 1;
    }

    auto ring = std::make_unique<StereoFrameRing<kRingCapacityFrames>>();
    StreamMeta meta;
    PersistentDecoderWorker decoder_worker(*ring, meta, matrix.storage_aux_core);

    unsigned int active_rate = 0;
    snd_pcm_uframes_t active_period = 0;
    std::vector<std::int32_t> out_buffer;
    std::uint64_t xruns = 0;
    std::uint64_t total_frames_written = 0;
    int last_alsa_err = 0;
    bool rt_warned = false;
    int exit_code = 0;

    for (std::size_t track_idx = 0;
         track_idx < queue.size() && !g_stop_requested.load(std::memory_order_acquire);
         ++track_idx) {
        // Drop back to SCHED_OTHER on Core 0 for preload I/O, ALSA setup, and console logging
        ApplyThreadAffinityAndPriority(matrix.audio_irq_core, 0, false);

        const std::string& flac_path = queue[track_idx];
        preloader.RefreshWindow(queue, track_idx);
        const auto* cached_entry = preloader.Find(flac_path);
        const std::vector<std::uint8_t>* preloaded_bytes =
            (cached_entry != nullptr) ? &cached_entry->bytes : nullptr;

        std::cout << "[QUEUE] Track " << (track_idx + 1) << "/" << queue.size()
                  << ": " << flac_path
                  << " (source=" << (preloaded_bytes ? "RAM" : "disk")
                  << ", cached_files=" << preloader.CachedFileCount()
                  << ", cached_mib=" << (preloader.TotalBytes() / (1024ULL * 1024ULL)) << ")\n";

        ring->Reset();
        meta.Reset();
        decoder_worker.StartTrack(flac_path, preloaded_bytes);

        while (!meta.meta_ready.load(std::memory_order_acquire) &&
               !meta.eof_or_error.load(std::memory_order_acquire) &&
               !g_stop_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }

        if (meta.eof_or_error.load(std::memory_order_acquire) ||
            g_stop_requested.load(std::memory_order_acquire)) {
            g_stop_requested.store(true, std::memory_order_release);
            decoder_worker.CancelCurrentTrack();
            decoder_worker.WaitTrackFinished();
            exit_code = 1;
            break;
        }

        const unsigned int stream_rate = meta.sample_rate.load(std::memory_order_acquire);
        const unsigned int bps = meta.bits_per_sample.load(std::memory_order_acquire);
        const unsigned int channels = meta.channels.load(std::memory_order_acquire);
        const unsigned int target_rate = flac_ax::ess::NegotiateEssRate(chip, stream_rate);

        std::cout << "[FLAC] Stream: " << stream_rate << " Hz, " << bps << "-bit, " << channels << " ch\n";

        // Pre-fill ring buffer before starting or re-locking ALSA output
        WaitForStartupWatermark(*ring, decoder_worker, meta, kStartupWatermarkFrames);

        if (target_rate != active_rate || active_period == 0) {
            if (active_rate != 0) {
                std::cout << "[ESS] Sample rate changed (" << active_rate << " Hz -> "
                          << target_rate << " Hz), draining & reconfiguring ALSA\n";
                snd_pcm_drain(pcm);
            }

            unsigned int actual_rate = 0;
            snd_pcm_uframes_t actual_period = 0;
            if (!ConfigureAlsaForEss(pcm, chip, stream_rate, period_frames, buffer_periods_override,
                                     actual_rate, actual_period)) {
                g_stop_requested.store(true, std::memory_order_release);
                decoder_worker.CancelCurrentTrack();
                decoder_worker.WaitTrackFinished();
                exit_code = 1;
                break;
            }
            active_rate = actual_rate;
            active_period = actual_period;
            out_buffer.assign(active_period * 2, 0);
}
        // Promote output thread to SCHED_FIFO 80 on Core 0 (zero stdio/allocations inside loop)
        ApplyThreadAffinityAndPriority(matrix.audio_irq_core, 80, !rt_warned);
        rt_warned = true;

        while (!g_stop_requested.load(std::memory_order_acquire)) {
            const std::size_t avail = ring->Size();
            if (avail == 0) {
                if (decoder_worker.IsFinished()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(150));
                continue;
            }

#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
            const auto cb_start = std::chrono::steady_clock::now();
#endif

            const std::size_t to_write = std::min<std::size_t>(avail, static_cast<std::size_t>(active_period));
            const std::size_t popped = ring->Pop(out_buffer.data(), to_write);
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
                    snd_pcm_wait(pcm, 5);
                } else if (written == -EPIPE) {
                    ++xruns;
#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
                    ax::GetTelemetry().audio_underruns.fetch_add(1, std::memory_order_relaxed);
#endif
                    const int rec_err = snd_pcm_recover(pcm, static_cast<int>(written), 1);
                    if (rec_err < 0) {
                        last_alsa_err = rec_err;
                        g_stop_requested.store(true, std::memory_order_release);
                        break;
                    }
                    WaitForStartupWatermark(*ring, decoder_worker, meta, kXrunRecoveryFrames);
                } else {
                    last_alsa_err = static_cast<int>(written);
                    g_stop_requested.store(true, std::memory_order_release);
                    break;
                }
            }

#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
            const auto cb_end = std::chrono::steady_clock::now();
            const auto cb_us = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(cb_end - cb_start).count()
            );
            ax::GetTelemetry().audio_buffer_switches.fetch_add(1, std::memory_order_relaxed);
            ax::RecordAudioCallbackMetrics(cb_us, static_cast<std::uint32_t>(ring->Size()));
#endif
        }

        decoder_worker.WaitTrackFinished();
        if (last_alsa_err < 0) {
            exit_code = 1;
            break;
        }
    }

    ApplyThreadAffinityAndPriority(matrix.audio_irq_core, 0, false);
    decoder_worker.Shutdown();

    if (last_alsa_err < 0) {
        std::cerr << "[ALSA] Fatal playback error: " << snd_strerror(last_alsa_err) << "\n";
    }

    if (pcm != nullptr) {
        if (!g_stop_requested.load(std::memory_order_acquire)) {
            snd_pcm_drain(pcm);
        } else {
            snd_pcm_drop(pcm);
        }
        snd_pcm_close(pcm);
    }

#if defined(FLAC_AX_HAVE_AX_TELEMETRY_HPP)
    const auto snap = ax::GetTelemetrySnapshot();
    std::cout << "[DONE] Chip=" << chip.chip_name
              << ", frames=" << total_frames_written
              << ", last_rate=" << active_rate << " Hz, xruns=" << xruns
              << ", jitter_us(min/p99/max)=" << snap.audio_jitter_min_us
              << "/" << snap.audio_jitter_p99_us
              << "/" << snap.audio_jitter_max_us
              << ", ring_low_water=" << snap.audio_ring_low_water_frames << "\n";
    ax::ShutdownTelemetryShm();
#else
    std::cout << "[DONE] Chip=" << chip.chip_name
              << ", frames=" << total_frames_written
              << ", last_rate=" << active_rate << " Hz, xruns=" << xruns << "\n";
#endif
    return exit_code;
}

