// src/ess_dac_hooks.hpp — ESS Sabre (ES9281AC Pro / ES9281PRO) hooks, DoP packer & ALSA mixer control
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#endif

namespace flac_ax::ess {

enum class EssChipFamily : std::uint8_t {
    Es9281AcPro = 0,
    Es9281Pro
};

struct EssChipProfile {
    EssChipFamily family{EssChipFamily::Es9281AcPro};
    const char* name{"ES9281AC Pro"};
    const char* default_alsa_device{"hw:CARD=FC3,DEV=0"};
    const char* default_mixer_card{"FC3"};
    unsigned int min_rate_hz{44100U};
    unsigned int max_rate_hz{384000U};
    unsigned int default_period_frames{96U};
    unsigned int fallback_period_frames{128U};
#if !defined(_WIN32)
    snd_pcm_format_t preferred_format{SND_PCM_FORMAT_S32_LE};
#endif
};

inline constexpr EssChipProfile MakeEssProfile(EssChipFamily family) noexcept {
    if (family == EssChipFamily::Es9281Pro) {
        return EssChipProfile{
            EssChipFamily::Es9281Pro,
            "ES9281PRO",
            "hw:CARD=FC3,DEV=0",
            "FC3",
            44100U,
            384000U,
            96U,
            128U
#if !defined(_WIN32)
            , SND_PCM_FORMAT_S32_LE
#endif
        };
    }
    return EssChipProfile{
        EssChipFamily::Es9281AcPro,
        "ES9281AC Pro",
        "hw:CARD=FC3,DEV=0",
        "FC3",
        44100U,
        384000U,
        96U,
        128U
#if !defined(_WIN32)
        , SND_PCM_FORMAT_S32_LE
#endif
    };
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
    }

    // Encodes a stream of raw DSD bytes (for a single channel) into DoP 32-bit words.
    // Returns number of 32-bit words written to `dst_words`.
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

    // Packs interleaved L/R raw DSD byte pairs into interleaved L/R DoP 32-bit words.
    // `src_lr_bytes` contains [L0, R0, L1, R1, ...]. `dst_lr_words` receives [L_dop0, R_dop0, ...].
    // Returns number of stereo DoP frames written.
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
        // Place 24-bit DoP payload in bits [31:8] (left-aligned for S32_LE container):
        // bits [31:24] = marker (0x05 / 0xFA), bits [23:16] = first_dsd_byte, bits [15:8] = second_dsd_byte
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
