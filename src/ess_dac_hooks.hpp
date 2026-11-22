// src/ess_dac_hooks.hpp — ESS Sabre (ES9281AC Pro / ES9281PRO) hooks, DSD/DoP packer & ALSA mixer control
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#endif

namespace flac_ax::ess {

enum class EssChipFamily : std::uint8_t {
    Es9281AcPro = 0,
    Es9281Pro
};

enum class AudioLoadTarget : std::uint8_t {
    Speakers = 0,       // 2.0 Vrms DAC -> Nobsound Q10 / NS-01G Pro (0.80 default)
    GradoSr325x,        // 38 Ohm open-back reference headphones (0.55 default)
    HeadphonesHighZ,    // ~120 Ohm studio open-back e.g. Sennheiser HD 560S (0.70 default)
    HeadphonesClosed,   // ~23-48 Ohm closed-back e.g. HD 569 / DJ 300 Pro X (0.50 default)
    LowOhmIem           // 16-32 Ohm sensitive IEMs / HD400S (0.22 default)
};

struct EssChipProfile {
    EssChipFamily family{EssChipFamily::Es9281AcPro};
    const char* chip_name{"ES9281AC Pro"};
    const char* name{"ES9281AC Pro"};
    const char* default_alsa_device{"hw:CARD=FC3,DEV=0"};
    const char* default_mixer_card{"FC3"};
    unsigned int min_rate_hz{44100U};
    unsigned int max_rate_hz{384000U};
    unsigned int default_period_frames{96U};
    unsigned int fallback_period_frames{128U};
    double recommended_volume_speakers{0.80};
    double recommended_volume_grado_sr325x{0.55};
    double recommended_volume_headphones_high_z{0.70};
    double recommended_volume_headphones_closed{0.50};
    double recommended_volume_low_ohm_iem{0.22};
    bool supports_dsd64{true};
    bool supports_dsd128{true};
    bool supports_dsd256{false};
#if !defined(_WIN32)
    snd_pcm_format_t preferred_format{SND_PCM_FORMAT_S32_LE};
#endif
};

inline constexpr EssChipProfile MakeEssProfile(EssChipFamily family) noexcept {
    if (family == EssChipFamily::Es9281Pro) {
        return EssChipProfile{
            EssChipFamily::Es9281Pro,
            "ES9281PRO",
            "ES9281PRO",
            "hw:CARD=FC3,DEV=0",
            "FC3",
            44100U,
            384000U,
            96U,
            128U,
            0.80,
            0.55,
            0.70,
            0.50,
            0.22,
            true,
            true,
            false
#if !defined(_WIN32)
            , SND_PCM_FORMAT_S32_LE
#endif
        };
    }
    return EssChipProfile{
        EssChipFamily::Es9281AcPro,
        "ES9281AC Pro",
        "ES9281AC Pro",
        "hw:CARD=FC3,DEV=0",
        "FC3",
        44100U,
        384000U,
        96U,
        128U,
        0.80,
        0.55,
        0.70,
        0.50,
        0.22,
        true,
        true,
        false
#if !defined(_WIN32)
        , SND_PCM_FORMAT_S32_LE
#endif
    };
}

inline std::optional<EssChipProfile> ResolveEssChipHook(std::string_view spec) noexcept {
    std::string lower;
    lower.reserve(spec.size());
    for (unsigned char ch : spec) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }

    if (lower.empty() || lower == "es9281ac_pro" || lower == "es9281ac-pro" ||
        lower == "es9281acpro" || lower == "fc3" || lower == "hiby_fc3") {
        return MakeEssProfile(EssChipFamily::Es9281AcPro);
    }
    if (lower == "es9281pro" || lower == "es9281_pro" || lower == "es9281-pro") {
        return MakeEssProfile(EssChipFamily::Es9281Pro);
    }
    return std::nullopt;
}

inline constexpr double RecommendedVolumeForTarget(const EssChipProfile& chip,
                                                 AudioLoadTarget target) noexcept {
    switch (target) {
        case AudioLoadTarget::GradoSr325x:
            return chip.recommended_volume_grado_sr325x;
        case AudioLoadTarget::HeadphonesHighZ:
            return chip.recommended_volume_headphones_high_z;
        case AudioLoadTarget::HeadphonesClosed:
            return chip.recommended_volume_headphones_closed;
        case AudioLoadTarget::LowOhmIem:
            return chip.recommended_volume_low_ohm_iem;
        case AudioLoadTarget::Speakers:
        default:
            return chip.recommended_volume_speakers;
    }
}

inline constexpr unsigned int NegotiateEssRate(const EssChipProfile& chip,
                                               unsigned int requested_rate_hz) noexcept {
    if (requested_rate_hz == 0U) {
        return 48000U;
    }
    return std::clamp(requested_rate_hz, chip.min_rate_hz, chip.max_rate_hz);
}

inline constexpr unsigned int RecommendedBufferPeriods(const EssChipProfile& chip,
                                                       unsigned int rate_hz,
                                                       unsigned int custom_periods = 0U) noexcept {
    (void)chip;
    if (custom_periods > 0U) {
        return std::clamp(custom_periods, 2U, 32U);
    }
    return (rate_hz >= 176400U) ? 8U : 4U;
}

#if !defined(_WIN32)
inline bool OnEssHardwareConfigured(const EssChipProfile& chip,
                                    snd_pcm_t* pcm,
                                    unsigned int actual_rate_hz,
                                    snd_pcm_uframes_t actual_period_frames) noexcept {
    (void)chip;
    (void)actual_rate_hz;
    (void)actual_period_frames;
    if (!pcm) {
        return false;
    }
    const snd_pcm_state_t state = snd_pcm_state(pcm);
    return state == SND_PCM_STATE_PREPARED || state == SND_PCM_STATE_RUNNING;
}
#endif

// Packs two consecutive 8-bit DSD samples (MSB-first within each byte) into a 24-bit DoP
// payload with alternating 0x05 / 0xFA marker bytes in bits [31:24] of a 32-bit word.
// - DSD64  (2.8224 MHz / channel) -> 176.4 kHz DoP frame clock
// - DSD128 (5.6448 MHz / channel) -> 352.8 kHz DoP frame clock
class DopPacker {
public:
    static constexpr std::uint8_t kMarkerA = 0x05U;
    static constexpr std::uint8_t kMarkerB = 0xFAU;

    void Reset() noexcept {
        toggle_ = false;
        has_pending_ = false;
        pending_byte_ = 0U;
        has_pending_stereo_ = false;
        pending_l_ = 0U;
        pending_r_ = 0U;
    }

    void PackStereoFrame(std::uint16_t dsd_l_msb_first,
                         std::uint16_t dsd_r_msb_first,
                         std::int32_t& out_l,
                         std::int32_t& out_r) noexcept {
        const std::uint8_t marker = NextMarker();
        const auto l0 = static_cast<std::uint8_t>((dsd_l_msb_first >> 8) & 0xFFU);
        const auto l1 = static_cast<std::uint8_t>(dsd_l_msb_first & 0xFFU);
        const auto r0 = static_cast<std::uint8_t>((dsd_r_msb_first >> 8) & 0xFFU);
        const auto r1 = static_cast<std::uint8_t>(dsd_r_msb_first & 0xFFU);
        out_l = static_cast<std::int32_t>(Pack24WithMarker(marker, l0, l1));
        out_r = static_cast<std::int32_t>(Pack24WithMarker(marker, r0, r1));
    }

    std::size_t PackBytes(const std::uint8_t* src_bytes,
                          std::size_t src_count,
                          std::uint32_t* dst_words,
                          std::size_t dst_capacity) noexcept {
        if (!src_bytes || !dst_words || dst_capacity == 0U) {
            return 0U;
        }

        std::size_t written = 0U;
        std::size_t i = 0U;

        if (has_pending_ && i < src_count && written < dst_capacity) {
            dst_words[written++] = MakeDopWord(pending_byte_, src_bytes[i++]);
            has_pending_ = false;
        }

        while ((i + 1U) < src_count && written < dst_capacity) {
            dst_words[written++] = MakeDopWord(src_bytes[i], src_bytes[i + 1U]);
            i += 2U;
        }

        if (i < src_count && !has_pending_) {
            pending_byte_ = src_bytes[i];
            has_pending_ = true;
        }

        return written;
    }

    std::size_t PackStereoInterleaved(const std::uint8_t* src_lr_bytes,
                                      std::size_t stereo_byte_pairs,
                                      std::int32_t* dst_lr_words,
                                      std::size_t dst_stereo_frame_capacity) noexcept {
        if (!src_lr_bytes || !dst_lr_words || dst_stereo_frame_capacity == 0U) {
            return 0U;
        }

        std::size_t frames_out = 0U;
        std::size_t pair_idx = 0U;

        if (has_pending_stereo_ && pair_idx < stereo_byte_pairs && frames_out < dst_stereo_frame_capacity) {
            const std::uint8_t l_b0 = pending_l_;
            const std::uint8_t r_b0 = pending_r_;
            const std::uint8_t l_b1 = src_lr_bytes[pair_idx * 2U];
            const std::uint8_t r_b1 = src_lr_bytes[pair_idx * 2U + 1U];
            const std::uint8_t marker = NextMarker();

            dst_lr_words[frames_out * 2U] = static_cast<std::int32_t>(Pack24WithMarker(marker, l_b0, l_b1));
            dst_lr_words[frames_out * 2U + 1U] = static_cast<std::int32_t>(Pack24WithMarker(marker, r_b0, r_b1));
            ++frames_out;
            ++pair_idx;
            has_pending_stereo_ = false;
        }

        while ((pair_idx + 1U) < stereo_byte_pairs && frames_out < dst_stereo_frame_capacity) {
            const std::uint8_t l_b0 = src_lr_bytes[pair_idx * 2U];
            const std::uint8_t r_b0 = src_lr_bytes[pair_idx * 2U + 1U];
            const std::uint8_t l_b1 = src_lr_bytes[(pair_idx + 1U) * 2U];
            const std::uint8_t r_b1 = src_lr_bytes[(pair_idx + 1U) * 2U + 1U];
            const std::uint8_t marker = NextMarker();

            dst_lr_words[frames_out * 2U] = static_cast<std::int32_t>(Pack24WithMarker(marker, l_b0, l_b1));
            dst_lr_words[frames_out * 2U + 1U] = static_cast<std::int32_t>(Pack24WithMarker(marker, r_b0, r_b1));
            ++frames_out;
            pair_idx += 2U;
        }

        if (pair_idx < stereo_byte_pairs && !has_pending_stereo_) {
            pending_l_ = src_lr_bytes[pair_idx * 2U];
            pending_r_ = src_lr_bytes[pair_idx * 2U + 1U];
            has_pending_stereo_ = true;
        }

        return frames_out;
    }

private:
    static std::uint32_t Pack24WithMarker(std::uint8_t marker,
                                          std::uint8_t first_dsd_byte,
                                          std::uint8_t second_dsd_byte) noexcept {
        return (static_cast<std::uint32_t>(marker) << 24) |
               (static_cast<std::uint32_t>(first_dsd_byte) << 16) |
               (static_cast<std::uint32_t>(second_dsd_byte) << 8);
    }

    std::uint8_t NextMarker() noexcept {
        const std::uint8_t m = toggle_ ? kMarkerB : kMarkerA;
        toggle_ = !toggle_;
        return m;
    }

    std::uint32_t MakeDopWord(std::uint8_t b0, std::uint8_t b1) noexcept {
        return Pack24WithMarker(NextMarker(), b0, b1);
    }

    bool toggle_{false};
    bool has_pending_{false};
    std::uint8_t pending_byte_{0U};

    bool has_pending_stereo_{false};
    std::uint8_t pending_l_{0U};
    std::uint8_t pending_r_{0U};
};

class EssHardwareMixer {
public:
    EssHardwareMixer() = default;
    ~EssHardwareMixer() { Close(); }

    EssHardwareMixer(const EssHardwareMixer&) = delete;
    EssHardwareMixer& operator=(const EssHardwareMixer&) = delete;

    bool Open(const char* card_name = "FC3") noexcept {
#if !defined(_WIN32)
        Close();
        if (!card_name || card_name[0] == '\0') {
            return false;
        }
        std::string dev = "hw:";
        dev += card_name;

        snd_mixer_t* mixer = nullptr;
        if (snd_mixer_open(&mixer, 0) < 0) {
            return false;
        }
        if (snd_mixer_attach(mixer, dev.c_str()) < 0) {
            snd_mixer_close(mixer);
            return false;
        }
        if (snd_mixer_selem_register(mixer, nullptr, nullptr) < 0) {
            snd_mixer_close(mixer);
            return false;
        }
        if (snd_mixer_load(mixer) < 0) {
            snd_mixer_close(mixer);
            return false;
        }

        snd_mixer_selem_id_t* sid = nullptr;
        snd_mixer_selem_id_alloca(&sid);

        static constexpr std::array<const char*, 4> kCandidateControls = {
            "PCM",
            "Playback",
            "Master",
            "DAC"
        };

        for (const char* name : kCandidateControls) {
            snd_mixer_selem_id_set_index(sid, 0);
            snd_mixer_selem_id_set_name(sid, name);
            snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
            if (elem && snd_mixer_selem_has_playback_volume(elem)) {
                mixer_ = mixer;
                elem_ = elem;
                snd_mixer_selem_get_playback_volume_range(elem_, &min_vol_, &max_vol_);
                return true;
            }
        }

        for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer);
             elem != nullptr;
             elem = snd_mixer_elem_next(elem)) {
            if (snd_mixer_selem_has_playback_volume(elem)) {
                mixer_ = mixer;
                elem_ = elem;
                snd_mixer_selem_get_playback_volume_range(elem_, &min_vol_, &max_vol_);
                return true;
            }
        }

        snd_mixer_close(mixer);
        return false;
#else
        (void)card_name;
        return true;
#endif
    }

    bool SetNormalizedVolume(double normalized_0_to_1) noexcept {
#if !defined(_WIN32)
        if (!mixer_ || !elem_ || max_vol_ <= min_vol_) {
            return false;
        }
        const double clamped = std::clamp(normalized_0_to_1, 0.0, 1.0);
        const double span = static_cast<double>(max_vol_ - min_vol_);
        const long target = min_vol_ + static_cast<long>(std::llround(clamped * span));
        return snd_mixer_selem_set_playback_volume_all(elem_, target) >= 0;
#else
        (void)normalized_0_to_1;
        return true;
#endif
    }

    void Close() noexcept {
#if !defined(_WIN32)
        if (mixer_) {
            snd_mixer_close(mixer_);
            mixer_ = nullptr;
        }
        elem_ = nullptr;
        min_vol_ = 0;
        max_vol_ = 0;
#endif
    }

private:
#if !defined(_WIN32)
    snd_mixer_t* mixer_{nullptr};
    snd_mixer_elem_t* elem_{nullptr};
    long min_vol_{0};
    long max_vol_{0};
#endif
};

} // namespace flac_ax::ess
```[cite: 52, 55]

### 2. `src/main.cpp`

```cpp
// src/main.cpp — Headless Bit-Perfect ESS Sabre FLAC & DoP (DSD64/DSD128) Player
#include "ess_dac_hooks.hpp"

#if !defined(_WIN32)
#include "dsd_dop_reader.hpp"
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
#else
#include <csignal>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if !defined(_WIN32) && defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/config.hpp>) && __has_include(<ax/telemetry.hpp>)
#include <ax/config.hpp>
#include <ax/telemetry.hpp>
#define FLAC_AX_MAIN_HAVE_TELEMETRY 1
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
    sa.sa_flags = 0; // Do not SA_RESTART so SIGUSR1 / SIGINT / SIGTERM wake promptly
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr); // Yield DAC cleanly when an emulator launches
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
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    if (fifo_prio > 0) {
        sched_param sp{};
        sp.sched_priority = fifo_prio;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    }
#else
    (void)core_id;
    (void)fifo_prio;
#endif
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

#if !defined(_WIN32)
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
        flac_ax::RamFilePreloader preloader(flac_ax::DetectHostPreloadPolicy());
        DsdTrackSession dsd_session;
        FlacRingDecoder flac_dec(ring_, meta_);
        std::vector<std::int32_t> dop_chunk(2048U * 2U, 0);

        for (std::size_t idx = 0; idx < tracks_.size(); ++idx) {
            if (!g_running.load(std::memory_order_acquire)) {
                break;
            }

            const std::string& path = tracks_[idx];
            dsd_session.Close();
            preloader.RefreshWindow(tracks_, idx);
            const flac_ax::RamFilePreloader::Entry* mem_entry = preloader.Find(path);

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
                         snd_pcm_uframes_t& out_
