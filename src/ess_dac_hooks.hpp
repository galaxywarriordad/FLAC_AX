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
    Speakers = 0,       // 2.0 Vrms HiBy FC3 -> Nobsound Q10 / NS-01G Pro (0.80 default)
    SpeakersFc3_1v3,    // 1.3 Vrms early HiBy FC3 revision -> Nobsound Q10 / NS-01G Pro (0.95 default)
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
    double output_vrms{2.0};
    double recommended_volume_speakers{0.80};
    double recommended_volume_speakers_1v3{0.95};
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

inline constexpr EssChipProfile MakeEssProfile(EssChipFamily family,
                                               double output_vrms = 2.0) noexcept {
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
            output_vrms,
            0.80,
            0.95,
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
        output_vrms,
        0.80,
        0.95,
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
        lower == "es9281acpro" || lower == "fc3" || lower == "hiby_fc3" ||
        lower == "fc3_2v0" || lower == "hiby_fc3_2v0") {
        return MakeEssProfile(EssChipFamily::Es9281AcPro, 2.0);
    }
    if (lower == "fc3_1v3" || lower == "hiby_fc3_1v3" || lower == "fc3-1.3v") {
        return MakeEssProfile(EssChipFamily::Es9281AcPro, 1.3);
    }
    if (lower == "es9281pro" || lower == "es9281_pro" || lower == "es9281-pro") {
        return MakeEssProfile(EssChipFamily::Es9281Pro, 2.0);
    }
    return std::nullopt;
}

inline constexpr double RecommendedVolumeForTarget(const EssChipProfile& chip,
                                                 AudioLoadTarget target) noexcept {
    switch (target) {
        case AudioLoadTarget::SpeakersFc3_1v3:
            return chip.recommended_volume_speakers_1v3;
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
            return (chip.output_vrms <= 1.35)
                ? chip.recommended_volume_speakers_1v3
                : chip.recommended_volume_speakers;
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
