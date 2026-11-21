// src/ess_dac_hooks.hpp — ESS Sabre (ES9281AC Pro / ES9281PRO) hooks for FLAC_AX
#pragma once

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace flac_ax::ess {

enum class EssChipFamily {
    Unknown,
    Es9281AcPro,
    Es9281Pro
};

struct EssChipProfile {
    EssChipFamily family{EssChipFamily::Unknown};
    const char* chip_name{"Generic ESS Sabre"};
    const char* default_alsa_device{"hw:CARD=FC3,DEV=0"};
    unsigned int min_rate_hz{44100};
    unsigned int max_rate_hz{384000};
    unsigned int preferred_rate_hz{48000};
    snd_pcm_format_t preferred_format{SND_PCM_FORMAT_S32_LE};
    snd_pcm_uframes_t default_period_frames{96};
    snd_pcm_uframes_t fallback_period_frames{128};
    unsigned int default_buffer_periods{4};
    bool supports_dop_dsd{true};
    bool prefers_direct_hw_rate{true};
};

struct ProfileMatrixCheck {
    bool file_found{false};
    bool has_v21_schema{false};
    bool common_audio_device_auto{false};
    std::string matched_cpu_profile;
    int audio_irq_core{0};
    int storage_aux_core{5};
};

struct DopPacker {
    bool use_0x05{true};

    // Packs two mono DSD payload words (16-bit big-endian per stereo channel) into
    // alternating 0x05 / 0xFA DoP markers in the top byte of S32_LE frames
    // (typically 176.4 kHz for DSD64 or 352.8 kHz for DSD128).
    void PackStereoFrame(std::uint16_t dsd_l_be,
                         std::uint16_t dsd_r_be,
                         std::int32_t& out_l_s32,
                         std::int32_t& out_r_s32) noexcept {
        const std::uint32_t marker = use_0x05 ? 0x05000000U : 0xFA000000U;
        use_0x05 = !use_0x05;

        const std::uint32_t payload_l = (static_cast<std::uint32_t>(dsd_l_be & 0xFFFFU) << 8);
        const std::uint32_t payload_r = (static_cast<std::uint32_t>(dsd_r_be & 0xFFFFU) << 8);

        out_l_s32 = static_cast<std::int32_t>(marker | payload_l);
        out_r_s32 = static_cast<std::int32_t>(marker | payload_r);
    }

    void Reset() noexcept {
        use_0x05 = true;
    }
};

class EssHardwareMixer {
public:
    EssHardwareMixer() = default;
    ~EssHardwareMixer() { Close(); }

    EssHardwareMixer(const EssHardwareMixer&) = delete;
    EssHardwareMixer& operator=(const EssHardwareMixer&) = delete;

    EssHardwareMixer(EssHardwareMixer&& other) noexcept
        : mixer_(other.mixer_),
          elem_(other.elem_),
          min_vol_(other.min_vol_),
          max_vol_(other.max_vol_) {
        other.mixer_ = nullptr;
        other.elem_ = nullptr;
        other.min_vol_ = 0;
        other.max_vol_ = 0;
    }

    EssHardwareMixer& operator=(EssHardwareMixer&& other) noexcept {
        if (this != &other) {
            Close();
            mixer_ = other.mixer_;
            elem_ = other.elem_;
            min_vol_ = other.min_vol_;
            max_vol_ = other.max_vol_;
            other.mixer_ = nullptr;
            other.elem_ = nullptr;
            other.min_vol_ = 0;
            other.max_vol_ = 0;
        }
        return *this;
    }

    bool Open(const char* card_name = "FC3") noexcept {
        Close();
        if (snd_mixer_open(&mixer_, 0) < 0) {
            mixer_ = nullptr;
            return false;
        }
        if (snd_mixer_attach(mixer_, card_name) < 0 ||
            snd_mixer_selem_register(mixer_, nullptr, nullptr) < 0 ||
            snd_mixer_load(mixer_) < 0) {
            Close();
            return false;
        }

        for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer_);
             elem != nullptr;
             elem = snd_mixer_elem_next(elem)) {
            if (snd_mixer_elem_get_type(elem) == SND_MIXER_ELEM_SIMPLE &&
                snd_mixer_selem_is_active(elem) &&
                snd_mixer_selem_has_playback_volume(elem)) {
                elem_ = elem;
                snd_mixer_selem_get_playback_volume_range(elem_, &min_vol_, &max_vol_);
                return true;
            }
        }

        Close();
        return false;
    }

    bool SetNormalizedVolume(double norm_0_to_1) noexcept {
        if (!elem_ || max_vol_ <= min_vol_) {
            return false;
        }
        const double clamped = std::clamp(norm_0_to_1, 0.0, 1.0);
        const long target = min_vol_ +
            static_cast<long>(std::llround(clamped * static_cast<double>(max_vol_ - min_vol_)));
        return snd_mixer_selem_set_playback_volume_all(elem_, target) >= 0;
    }

    void Close() noexcept {
        if (mixer_) {
            snd_mixer_close(mixer_);
            mixer_ = nullptr;
        }
        elem_ = nullptr;
        min_vol_ = 0;
        max_vol_ = 0;
    }

private:
    snd_mixer_t* mixer_{nullptr};
    snd_mixer_elem_t* elem_{nullptr};
    long min_vol_{0};
    long max_vol_{0};
};

inline EssChipProfile MakeEssProfile(EssChipFamily family) noexcept {
    switch (family) {
        case EssChipFamily::Es9281AcPro:
            return EssChipProfile{
                .family = EssChipFamily::Es9281AcPro,
                .chip_name = "ES9281AC Pro",
                .default_alsa_device = "hw:CARD=FC3,DEV=0",
                .min_rate_hz = 44100,
                .max_rate_hz = 384000,
                .preferred_rate_hz = 48000,
                .preferred_format = SND_PCM_FORMAT_S32_LE,
                .default_period_frames = 96,
                .fallback_period_frames = 128,
                .default_buffer_periods = 4,
                .supports_dop_dsd = true,
                .prefers_direct_hw_rate = true,
            };
        case EssChipFamily::Es9281Pro:
            return EssChipProfile{
                .family = EssChipFamily::Es9281Pro,
                .chip_name = "ES9281PRO",
                .default_alsa_device = "hw:CARD=FC3,DEV=0",
                .min_rate_hz = 44100,
                .max_rate_hz = 384000,
                .preferred_rate_hz = 48000,
                .preferred_format = SND_PCM_FORMAT_S32_LE,
                .default_period_frames = 96,
                .fallback_period_frames = 128,
                .default_buffer_periods = 4,
                .supports_dop_dsd = true,
                .prefers_direct_hw_rate = true,
            };
        case EssChipFamily::Unknown:
        default:
            return EssChipProfile{};
    }
}

inline std::optional<EssChipProfile> ResolveEssChipHook(std::string_view spec) noexcept {
    std::string lower(spec);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("9281ac") != std::string::npos || lower.find("fc3") != std::string::npos) {
        return MakeEssProfile(EssChipFamily::Es9281AcPro);
    }
    if (lower.find("9281pro") != std::string::npos || lower.find("es9281") != std::string::npos) {
        return MakeEssProfile(EssChipFamily::Es9281Pro);
    }
    return std::nullopt;
}

inline unsigned int NegotiateEssRate(const EssChipProfile& chip, unsigned int stream_rate_hz) noexcept {
    if (stream_rate_hz == 0U) {
        return chip.preferred_rate_hz;
    }
    return std::clamp(stream_rate_hz, chip.min_rate_hz, chip.max_rate_hz);
}

// Scales buffer periods for ultra-high sample rates (176.4 kHz+ / DSD DoP) so hardware
// buffer duration doesn't shrink below ~2 ms and risk USB bus jitter underruns.
inline unsigned int RecommendedBufferPeriods(const EssChipProfile& chip,
                                             unsigned int rate_hz,
                                             unsigned int user_override = 0) noexcept {
    if (user_override > 0) {
        return user_override;
    }
    if (rate_hz >= 176400U) {
        return std::max(chip.default_buffer_periods, 8U);
    }
    return chip.default_buffer_periods;
}

inline bool OnEssHardwareConfigured(const EssChipProfile& chip,
                                  snd_pcm_t* pcm,
                                  unsigned int actual_rate_hz,
                                  snd_pcm_uframes_t actual_period_frames) noexcept {
    (void)chip;
    if (!pcm || actual_rate_hz == 0U || actual_period_frames == 0U) {
        return false;
    }
    const snd_pcm_state_t st = snd_pcm_state(pcm);
    return st == SND_PCM_STATE_PREPARED || st == SND_PCM_STATE_RUNNING;
}

inline ProfileMatrixCheck CrossCheckHardwareProfiles(
    const std::filesystem::path& profiles_path,
    std::string_view cpu_model_hint = "i5-9600K") {
    ProfileMatrixCheck check{};
    std::ifstream in(profiles_path);
    if (!in.is_open()) {
        return check;
    }
    check.file_found = true;

    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());

    check.has_v21_schema = (content.find("\"schema_version\": \"2.1\"") != std::string::npos);
    check.common_audio_device_auto =
        (content.find("\"Audio|output_device\": \"auto\"") != std::string::npos);

    if (!cpu_model_hint.empty() &&
        content.find(std::string(cpu_model_hint)) != std::string::npos) {
        check.matched_cpu_profile = std::string(cpu_model_hint);
    }
    return check;
}

} // namespace flac_ax::ess
```
