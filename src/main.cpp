// src/main.cpp — Deterministic Bit-Perfect ESS Sabre FLAC, DoP (DSD) & Tracker Module Transport
// Topology: Core 5 (RamFilePreloader + FLAC/DSD/OpenMPT Decoder) -> SPSC Ring -> Core 0 (SCHED_FIFO 80 ALSA S32_LE)
#include "audio/ax_dac_lock.hpp"
#include "audio/ax_stereo_ring.hpp"
#include "dsd_dop_reader.hpp"
#include "ess_dac_hooks.hpp"
#include "preload_policy.hpp"
#include "tracker_openmpt_reader.hpp"

#if !defined(_WIN32)
#include <FLAC++/decoder.h>
#include <FLAC/metadata.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#define FLAC_AX_MAIN_HAS_FLAC_AND_ALSA 1
#else
#include <csignal>
#if defined(FLAC_AX_HAS_FLACPP)
#include <FLAC++/decoder.h>
#include <FLAC/metadata.h>
#define FLAC_AX_MAIN_HAS_FLACPP 1
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/audio_ring.hpp>)
#include <ax/audio_ring.hpp>
#define FLAC_AX_HAVE_SDK_RING 1
#endif
#if __has_include(<ax/config.hpp>) && __has_include(<ax/telemetry.hpp>)
#include <ax/config.hpp>
#include <ax/telemetry.hpp>
#define FLAC_AX_HAVE_SDK_TELEMETRY 1
#endif
#endif

namespace {

std::atomic<bool> g_running{true};

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

bool ParseInt32(std::string_view text, std::int32_t& out) noexcept {
    if (text.empty()) return false;
    std::int32_t value = 0;
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

bool IsSupportedAudioExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".flac" || ext == ".dsf" || ext == ".dff" ||
           flac_ax::tracker::IsTrackerExtension(path);
}

#if defined(FLAC_AX_MAIN_HAS_FLAC_AND_ALSA)
bool IsDsdExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".dsf" || ext == ".dff";
}
#endif

void CollectTracks(const std::vector<std::string>& inputs,
                   std::vector<std::filesystem::path>& out_tracks) {
    for (const auto& item : inputs) {
        std::error_code ec;
        std::filesystem::path p(item);
        if (std::filesystem::is_directory(p, ec)) {
            std::vector<std::filesystem::path> dir_entries;
            for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                if (!ec && entry.is_regular_file(ec) && IsSupportedAudioExtension(entry.path())) {
                    dir_entries.push_back(entry.path());
                }
            }
            std::sort(dir_entries.begin(), dir_entries.end());
            out_tracks.insert(out_tracks.end(), dir_entries.begin(), dir_entries.end());
        } else if (std::filesystem::is_regular_file(p, ec) && IsSupportedAudioExtension(p)) {
            out_tracks.push_back(p);
        }
    }
}

#if defined(FLAC_AX_MAIN_HAS_FLAC_AND_ALSA)
struct StreamFormatSpec {
    unsigned int sample_rate_hz{48000U};
    unsigned int source_bits{24U};
    bool is_dop{false};
    bool is_tracker{false};
    std::string label;
};

inline std::int32_t SampleToS32(FLAC__int32 sample, unsigned int bps) noexcept {
    if (bps >= 32U) {
        return static_cast<std::int32_t>(sample);
    }
    const unsigned int shift = 32U - std::max(1U, bps);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(sample) << shift);
}

class FlacStreamDecoder final : public FLAC::Decoder::Stream {
public:
    FlacStreamDecoder(flac_ax::audio::StereoFrameRing32& ring,
                      const std::atomic<bool>& running,
                      const std::atomic<bool>& abort_track) noexcept
        : ring_(ring), running_(running), abort_track_(abort_track) {}

    bool DecodeMemory(const std::uint8_t* data, std::size_t size, StreamFormatSpec& out_spec) {
        mem_data_ = data;
        mem_size_ = size;
        mem_pos_ = 0U;
        file_.close();
        sample_rate_ = 0U;
        channels_ = 2U;
        bps_ = 16U;

        if (init() != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            return false;
        }
        if (!process_until_end_of_metadata()) {
            finish();
            return false;
        }
        out_spec.sample_rate_hz = (sample_rate_ > 0U) ? sample_rate_ : 48000U;
        out_spec.source_bits = bps_;
        out_spec.is_dop = false;
        out_spec.is_tracker = false;

        const bool ok = process_until_end_of_stream();
        finish();
        return ok;
    }

    bool DecodeFile(const std::filesystem::path& path, StreamFormatSpec& out_spec) {
        mem_data_ = nullptr;
        mem_size_ = 0U;
        mem_pos_ = 0U;
        file_.close();
        file_.open(path, std::ios::binary);
        if (!file_.is_open()) {
            return false;
        }
        sample_rate_ = 0U;
        channels_ = 2U;
        bps_ = 16U;

        if (init() != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            file_.close();
            return false;
        }
        if (!process_until_end_of_metadata()) {
            finish();
            file_.close();
            return false;
        }
        out_spec.sample_rate_hz = (sample_rate_ > 0U) ? sample_rate_ : 48000U;
        out_spec.source_bits = bps_;
        out_spec.is_dop = false;
        out_spec.is_tracker = false;

        const bool ok = process_until_end_of_stream();
        finish();
        file_.close();
        return ok;
    }

protected:
    FLAC__StreamDecoderReadStatus read_callback(FLAC__byte buffer[], std::size_t* bytes) override {
        if (!running_.load(std::memory_order_acquire) || abort_track_.load(std::memory_order_acquire)) {
            *bytes = 0U;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        if (mem_data_ != nullptr) {
            if (mem_pos_ >= mem_size_) {
                *bytes = 0U;
                return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
            }
            const std::size_t to_copy = std::min(*bytes, mem_size_ - mem_pos_);
            std::memcpy(buffer, mem_data_ + mem_pos_, to_copy);
            mem_pos_ += to_copy;
            *bytes = to_copy;
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        }
        if (!file_.is_open()) {
            *bytes = 0U;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(*bytes));
        const auto got = static_cast<std::size_t>(file_.gcount());
        *bytes = got;
        if (got == 0U) {
            return file_.eof() ? FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM
                               : FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    void metadata_callback(const FLAC__StreamMetadata* metadata) override {
        if (metadata && metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            sample_rate_ = metadata->data.stream_info.sample_rate;
            channels_ = metadata->data.stream_info.channels;
            bps_ = metadata->data.stream_info.bits_per_sample;
        }
    }

    FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[]) override {
        if (!running_.load(std::memory_order_acquire) || abort_track_.load(std::memory_order_acquire)) {
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        const std::size_t block_frames = frame->header.blocksize;
        const unsigned int bps = (frame->header.bits_per_sample > 0U)
                                     ? frame->header.bits_per_sample
                                     : bps_;
        scratch_.resize(block_frames * 2U);
        for (std::size_t i = 0U; i < block_frames; ++i) {
            const FLAC__int32 l_raw = buffer[0][i];
            const FLAC__int32 r_raw = (channels_ >= 2U) ? buffer[1][i] : l_raw;
            scratch_[i * 2U + 0U] = SampleToS32(l_raw, bps);
            scratch_[i * 2U + 1U] = SampleToS32(r_raw, bps);
        }

        std::size_t pushed = 0U;
        while (pushed < block_frames &&
               running_.load(std::memory_order_acquire) &&
               !abort_track_.load(std::memory_order_acquire)) {
            pushed += ring_.PushInterleaved(scratch_.data() + pushed * 2U, block_frames - pushed);
            if (pushed < block_frames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void error_callback(FLAC__StreamDecoderErrorStatus /*status*/) override {}

private:
    flac_ax::audio::StereoFrameRing32& ring_;
    const std::atomic<bool>& running_;
    const std::atomic<bool>& abort_track_;

    const std::uint8_t* mem_data_{nullptr};
    std::size_t mem_size_{0U};
    std::size_t mem_pos_{0U};
    std::ifstream file_;

    unsigned int sample_rate_{0U};
    unsigned int channels_{2U};
    unsigned int bps_{16U};
    std::vector<std::int32_t> scratch_;
};

bool ConfigureAlsaDevice(snd_pcm_t* pcm,
                         const flac_ax::ess::EssChipProfile& chip,
                         unsigned int requested_rate_hz,
                         snd_pcm_uframes_t preferred_period,
                         unsigned int& out_rate_hz,
                         snd_pcm_uframes_t& out_period_frames) {
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);

    if (snd_pcm_hw_params_any(pcm, params) < 0) return false;
    if (snd_pcm_hw_params_set_rate_resample(pcm, params, 0) < 0) return false;
    if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) return false;
    if (snd_pcm_hw_params_set_format(pcm, params, chip.preferred_format) < 0) return false;
    if (snd_pcm_hw_params_set_channels(pcm, params, 2) < 0) return false;

    unsigned int rate = flac_ax::ess::NegotiateEssRate(chip, requested_rate_hz);
    int dir = 0;
    if (snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir) < 0) return false;

    snd_pcm_uframes_t period = preferred_period;
    if (snd_pcm_hw_params_set_period_size_near(pcm, params, &period, &dir) < 0) return false;

    const unsigned int periods = flac_ax::ess::RecommendedBufferPeriods(chip, rate, 0U);
    snd_pcm_uframes_t buffer_size = period * periods;
    if (snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer_size) < 0) return false;

    if (snd_pcm_hw_params(pcm, params) < 0) return false;
    if (snd_pcm_prepare(pcm) < 0) return false;

    out_rate_hz = rate;
    out_period_frames = period;
    return flac_ax::ess::OnEssHardwareConfigured(chip, pcm, out_rate_hz, out_period_frames);
}
#endif

void PrintUsage() {
    std::cerr
        << "Usage: flac_ax [options] <file_or_dir> [...]\n"
        << "  --device <alsa_dev>       ALSA PCM device (default: hw:CARD=FC3,DEV=0)\n"
        << "  --mixer-card <card>       ALSA mixer card (default: FC3)\n"
        << "  --volume <0.0..1.0>       Set ESS hardware mixer playback level\n"
        << "  --chip <profile>          DAC profile: fc3_2v0 (default), fc3_1v3, es9281pro\n"
        << "  --period <frames>         Preferred ALSA period frames (default: 96)\n"
        << "  --mod-rate <hz>           Sample rate for .mod/.xm/.s3m/.it modules (default: 48000)\n"
        << "  --mod-stereo <0..200>     Amiga/ST tracker stereo separation % (default: 75)\n"
        << "  --mod-interp <1|2|4|8>    Tracker resampler filter taps (1=Amiga raw, 8=sinc, default: 8)\n"
        << "  --mod-loop                Loop tracker module infinitely until SIGINT/next\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string device_override;
    std::string mixer_card = "FC3";
    std::string chip_spec = "fc3_2v0";
    bool has_volume = false;
    double target_volume = 0.80;
    std::uint32_t period_frames_u32 = 96U;
    std::uint32_t mod_rate_hz = 48000U;
    flac_ax::tracker::TrackerRenderConfig tracker_cfg{};
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            device_override = argv[++i];
        } else if (arg == "--mixer-card" && i + 1 < argc) {
            mixer_card = argv[++i];
        } else if (arg == "--volume" && i + 1 < argc) {
            if (!ParseDouble(argv[++i], target_volume)) {
                PrintUsage();
                return 1;
            }
            has_volume = true;
        } else if (arg == "--chip" && i + 1 < argc) {
            chip_spec = argv[++i];
        } else if (arg == "--period" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], period_frames_u32) || period_frames_u32 == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--mod-rate" && i + 1 < argc) {
            if (!ParseUint32(argv[++i], mod_rate_hz) || mod_rate_hz == 0U) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--mod-stereo" && i + 1 < argc) {
            std::int32_t sep = 75;
            if (!ParseInt32(argv[++i], sep)) {
                PrintUsage();
                return 1;
            }
            tracker_cfg.stereo_separation_pct = sep;
        } else if (arg == "--mod-interp" && i + 1 < argc) {
            std::int32_t interp = 8;
            if (!ParseInt32(argv[++i], interp)) {
                PrintUsage();
                return 1;
            }
            tracker_cfg.interpolation_filter = interp;
        } else if (arg == "--mod-loop") {
            tracker_cfg.repeat_count = -1;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        } else if (!arg.empty() && arg == "-") {
            PrintUsage();
            return 1;
        } else {
            inputs.emplace_back(arg);
        }
    }

    if (inputs.empty()) {
        PrintUsage();
        return 1;
    }

    std::vector<std::filesystem::path> tracks;
    CollectTracks(inputs, tracks);
    if (tracks.empty()) {
        std::cerr << "[FLAC_AX] No supported audio files (.flac/.dsf/.dff/.mod/.xm/.s3m/.it) found.\n";
        return 1;
    }

    InstallSignals();

#if !defined(FLAC_AX_MAIN_HAS_FLAC_AND_ALSA)
    (void)device_override;
    (void)mixer_card;
    (void)chip_spec;
    (void)has_volume;
    (void)target_volume;
    (void)period_frames_u32;
    (void)mod_rate_hz;
    (void)tracker_cfg;
    std::cout << "[FLAC_AX] Windows build mode: ALSA/FLAC++ hardware transport skipped (use Linux/Piccolo-AX for hardware I/O)\n";
    return 0;
#else
    std::vector<std::string> track_strings;
    track_strings.reserve(tracks.size());
    for (const auto& t : tracks) {
        track_strings.push_back(t.string());
    }

    const auto chip_opt = flac_ax::ess::ResolveEssChipHook(chip_spec);
    if (!chip_opt.has_value()) {
        std::cerr << "[FLAC_AX] Unknown --chip profile '" << chip_spec << "'\n";
        return 1;
    }
    const auto chip = *chip_opt;
    const std::string alsa_dev = !device_override.empty() ? device_override : chip.default_alsa_device;

    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[FLAC_AX] WARN: mlockall skipped (check CAP_IPC_LOCK / RLIMIT_MEMLOCK)\n";
    }

    flac_ax::audio::ScopedDacLock dac_lock;
    if (!dac_lock.Acquire("/run/lock/ax-dac.lock", true)) {
        std::cerr << "[FLAC_AX] DAC lock busy (/run/lock/ax-dac.lock held by another process)\n";
        return 1;
    }

    flac_ax::ess::EssHardwareMixer hw_mixer;
    if (has_volume && hw_mixer.Open(mixer_card.c_str())) {
        (void)hw_mixer.SetNormalizedVolume(target_volume);
    }

#if defined(FLAC_AX_HAVE_SDK_TELEMETRY)
    ax::EngineConfig ax_cfg{};
    (void)ax::LoadEngineConfig("ax_engine.ini", ax_cfg);
    if (ax_cfg.enable_shared_memory_export) {
        (void)ax::InitTelemetryShm();
    }
    ax::ResetTelemetry();
#endif

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, alsa_dev.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "[FLAC_AX] Failed to open ALSA device '" << alsa_dev << "'\n";
#if defined(FLAC_AX_HAVE_SDK_TELEMETRY)
        ax::ShutdownTelemetryShm();
#endif
        return 1;
    }

    const auto preload_policy = flac_ax::DetectHostPreloadPolicy();
    std::cout << "[FLAC_AX] Queue: " << tracks.size() << " track(s)"
              << " | Preload mode: " << flac_ax::ToString(preload_policy.mode)
              << " (cap=" << (preload_policy.max_total_bytes / (1024ULL * 1024ULL)) << " MiB)"
              << " | OpenMPT: " << (flac_ax::tracker::OpenMptModuleStreamer::IsSupported() ? "enabled" : "disabled")
              << "\n";

    flac_ax::audio::StereoFrameRing32 ring;
    std::atomic<bool> abort_track{false};
    std::atomic<bool> playlist_done{false};
    std::atomic<bool> format_ready{false};
    std::atomic<unsigned int> active_rate_hz{48000U};
    std::atomic<std::uint64_t> track_generation{0ULL};
    std::atomic<std::uint64_t> xrun_count{0ULL};

    std::thread decoder_thread([&]() {
        PinThread(5, 0);
        flac_ax::RamFilePreloader preloader(preload_policy);
        FlacStreamDecoder flac_decoder(ring, g_running, abort_track);
        std::vector<std::int32_t> decode_chunk(4096U * 2U, 0);

        for (std::size_t idx = 0U; idx < tracks.size() && g_running.load(std::memory_order_acquire); ++idx) {
            abort_track.store(false, std::memory_order_release);
            preloader.RefreshWindow(track_strings, idx);
            const auto& path = tracks[idx];
            const auto* cached = preloader.Find(path.string());

            if (flac_ax::tracker::IsTrackerExtension(path)) {
                flac_ax::tracker::OpenMptModuleStreamer mod_stream;
                const unsigned int target_mod_rate = flac_ax::ess::NegotiateEssRate(chip, mod_rate_hz);
                const bool opened = cached
                    ? mod_stream.OpenFromMemory(cached->bytes.data(), cached->bytes.size(), target_mod_rate, tracker_cfg)
                    : mod_stream.OpenFromFile(path, target_mod_rate, tracker_cfg);
                if (!opened) {
                    std::cerr << "[FLAC_AX] WARN: Skipping unreadable tracker module: " << path.filename() << "\n";
                    continue;
                }

                while (ring.AvailableToRead() > 0U && g_running.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                const auto& meta = mod_stream.metadata();
                std::cout << "[FLAC_AX] [TRACKER] " << path.filename().string()
                          << " | Title: '" << (meta.title.empty() ? "Untitled" : meta.title) << "'"
                          << " | Engine: " << (meta.tracker.empty() ? meta.type_long : meta.tracker)
                          << " | " << meta.num_channels << "ch @ " << target_mod_rate << " Hz S32_LE\n";

                active_rate_hz.store(target_mod_rate, std::memory_order_release);
                track_generation.fetch_add(1ULL, std::memory_order_acq_rel);
                format_ready.store(true, std::memory_order_release);

                while (g_running.load(std::memory_order_acquire) && !abort_track.load(std::memory_order_acquire)) {
                    const std::size_t got = mod_stream.ReadInterleavedS32(decode_chunk.data(), 4096U);
                    if (got == 0U) {
                        break;
                    }
                    std::size_t pushed = 0U;
                    while (pushed < got && g_running.load(std::memory_order_acquire)) {
                        pushed += ring.PushInterleaved(decode_chunk.data() + pushed * 2U, got - pushed);
                        if (pushed < got) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                    }
                }
                mod_stream.Close();
            } else if (IsDsdExtension(path)) {
                while (ring.AvailableToRead() > 0U && g_running.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                flac_ax::dsd::DsdStreamInfo dsd_info{};
                const bool parsed = cached
                    ? flac_ax::dsd::ParseDsdMemoryHeader(
                          cached->bytes.data(),
                          static_cast<std::uint64_t>(cached->bytes.size()),
                          dsd_info)
                    : flac_ax::dsd::ParseDsdFileHeader(path.string(), dsd_info);
                if (!parsed) {
                    std::cerr << "[FLAC_AX] WARN: Skipping unsupported/unreadable DSD file: " << path.filename() << "\n";
                    continue;
                }

                FILE* disk_fp = nullptr;
                if (!cached) {
                    disk_fp = std::fopen(path.c_str(), "rb");
                    if (!disk_fp) {
                        std::cerr << "[FLAC_AX] WARN: Failed opening DSD file: " << path.filename() << "\n";
                        continue;
                    }
                }

                flac_ax::dsd::DsdDopStreamer dsd_stream;
                dsd_stream.Configure(
                    dsd_info,
                    cached ? cached->bytes.data() : nullptr,
                    cached ? static_cast<std::uint64_t>(cached->bytes.size()) : 0ULL,
                    disk_fp);

                const unsigned int dop_rate = dsd_info.dop_pcm_rate_hz;
                std::cout << "[FLAC_AX] [DoP DSD] " << path.filename().string()
                          << " @ " << dop_rate << " Hz DoP S32_LE\n";

                active_rate_hz.store(dop_rate, std::memory_order_release);
                track_generation.fetch_add(1ULL, std::memory_order_acq_rel);
                format_ready.store(true, std::memory_order_release);

                while (g_running.load(std::memory_order_acquire) && !abort_track.load(std::memory_order_acquire)) {
                    const std::size_t got = dsd_stream.ReadDopFramesS32(decode_chunk.data(), 4096U);
                    if (got == 0U) {
                        break;
                    }
                    std::size_t pushed = 0U;
                    while (pushed < got && g_running.load(std::memory_order_acquire)) {
                        pushed += ring.PushInterleaved(decode_chunk.data() + pushed * 2U, got - pushed);
                        if (pushed < got) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                    }
                }

                if (disk_fp != nullptr) {
                    std::fclose(disk_fp);
                }
            } else {
                while (ring.AvailableToRead() > 0U && g_running.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                FLAC__StreamMetadata si{};
                unsigned int file_rate = 48000U;
                unsigned int file_bps = 16U;
                if (FLAC__metadata_get_streaminfo(path.c_str(), &si)) {
                    file_rate = si.data.stream_info.sample_rate;
                    file_bps  = si.data.stream_info.bits_per_sample;
                }

                std::cout << "[FLAC_AX] [FLAC] " << path.filename().string()
                          << " (" << file_bps << "-bit @ " << file_rate << " Hz -> S32_LE"
                          << (cached ? ", RAM-locked" : ", streamed") << ")\n";

                active_rate_hz.store(file_rate, std::memory_order_release);
                track_generation.fetch_add(1ULL, std::memory_order_acq_rel);
                format_ready.store(true, std::memory_order_release);

                StreamFormatSpec spec{};
                const bool ok = cached
                    ? flac_decoder.DecodeMemory(cached->bytes.data(), cached->bytes.size(), spec)
                    : flac_decoder.DecodeFile(path, spec);
                if (!ok && g_running.load(std::memory_order_acquire)) {
                    std::cerr << "[FLAC_AX] WARN: FLAC decode error on " << path.filename() << "\n";
                }
            }
        }

        playlist_done.store(true, std::memory_order_release);
    });

    PinThread(0, 80);

    while (g_running.load(std::memory_order_acquire) &&
           !format_ready.load(std::memory_order_acquire) &&
           !playlist_done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::uint64_t configured_gen = 0ULL;
    unsigned int current_hw_rate = 0U;
    snd_pcm_uframes_t actual_period = static_cast<snd_pcm_uframes_t>(period_frames_u32);
    std::vector<std::int32_t> period_buf(static_cast<std::size_t>(actual_period) * 2U, 0);

    while (g_running.load(std::memory_order_acquire)) {
        const std::uint64_t gen = track_generation.load(std::memory_order_acquire);
        const unsigned int req_rate = active_rate_hz.load(std::memory_order_acquire);

        if (gen != configured_gen && req_rate > 0U) {
            if (current_hw_rate != req_rate) {
                (void)snd_pcm_drain(pcm);
                if (!ConfigureAlsaDevice(pcm, chip, req_rate,
                                         static_cast<snd_pcm_uframes_t>(period_frames_u32),
                                         current_hw_rate, actual_period)) {
                    std::cerr << "[FLAC_AX] Failed to lock ALSA rate " << req_rate << " Hz\n";
                    g_running.store(false, std::memory_order_release);
                    break;
                }
                period_buf.assign(static_cast<std::size_t>(actual_period) * 2U, 0);
            }
            configured_gen = gen;
        }

        const std::size_t avail = ring.AvailableToRead();
        if (avail == 0U) {
            if (playlist_done.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

#if defined(FLAC_AX_HAVE_SDK_TELEMETRY)
        const auto t0 = std::chrono::steady_clock::now();
#endif
        const std::size_t to_pop = std::min<std::size_t>(avail, actual_period);
        const std::size_t popped = ring.PopInterleaved(period_buf.data(), to_pop);
        if (popped < actual_period) {
            std::fill(period_buf.begin() + popped * 2U, period_buf.end(), 0);
        }

        std::size_t offset = 0U;
        while (offset < actual_period && g_running.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, period_buf.data() + offset * 2U, actual_period - offset);
            if (written >= 0) {
                offset += static_cast<std::size_t>(written);
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

#if defined(FLAC_AX_HAVE_SDK_TELEMETRY)
        const auto us = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count());
        ax::RecordAudioCallbackMetrics(us, static_cast<std::uint32_t>(actual_period));
#endif
    }

    g_running.store(false, std::memory_order_release);
    if (decoder_thread.joinable()) {
        decoder_thread.join();
    }

    (void)snd_pcm_drain(pcm);
    (void)snd_pcm_close(pcm);
    hw_mixer.Close();

#if defined(FLAC_AX_HAVE_SDK_TELEMETRY)
    ax::ShutdownTelemetryShm();
#endif
    return 0;
#endif
