// src/tracker_openmpt_reader.hpp — Bit-perfect Amiga/Atari ST tracker module reader (.mod/.xm/.s3m/.it) via libopenmpt
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(FLAC_AX_HAS_OPENMPT)
#include <libopenmpt/libopenmpt.h>
#endif

namespace flac_ax::tracker {

struct TrackerRenderConfig {
    int repeat_count{0};            // 0 = play once, -1 = infinite loop
    int stereo_separation_pct{75};  // 100 = full hard-pan Amiga Paula LRRL, 75 = natural headphone/nearfield crossfeed
    int interpolation_filter{8};    // 1 = none (raw Paula), 2 = linear, 4 = cubic, 8 = windowed sinc
    int subsong{-1};                // -1 = all subsongs sequentially, >=0 = specific subsong index
    double master_gain_db{0.0};     // Pre-DAC digital headroom trim in dB
};

struct TrackerMetadata {
    std::string title;
    std::string tracker;
    std::string type_long;
    int num_channels{0};
    int num_orders{0};
    int num_patterns{0};
    int num_subsongs{1};
    double duration_sec{0.0};
};

inline bool IsTrackerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    static constexpr std::array<std::string_view, 14> kExts = {
        ".mod", ".xm", ".s3m", ".it", ".mptm", ".med", ".okt",
        ".669", ".stm", ".mtm", ".far", ".ult", ".dmf", ".amf"
    };
    for (std::string_view candidate : kExts) {
        if (ext == candidate) {
            return true;
        }
    }
    return false;
}

class OpenMptModuleStreamer {
public:
    OpenMptModuleStreamer() = default;
    ~OpenMptModuleStreamer() { Close(); }

    OpenMptModuleStreamer(const OpenMptModuleStreamer&) = delete;
    OpenMptModuleStreamer& operator=(const OpenMptModuleStreamer&) = delete;

    static constexpr bool IsSupported() noexcept {
#if defined(FLAC_AX_HAS_OPENMPT)
        return true;
#else
        return false;
#endif
    }

    bool OpenFromMemory(const std::uint8_t* data,
                        std::size_t size,
                        unsigned int sample_rate_hz,
                        const TrackerRenderConfig& cfg = TrackerRenderConfig{}) {
        Close();
#if defined(FLAC_AX_HAS_OPENMPT)
        if (!data || size == 0U || sample_rate_hz == 0U) {
            return false;
        }
        mod_ = openmpt_module_create_from_memory2(
            data,
            size,
            openmpt_log_func_silent,
            nullptr,
            openmpt_error_func_ignore,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (!mod_) {
            return false;
        }

        sample_rate_ = sample_rate_hz;
        (void)openmpt_module_set_repeat_count(mod_, cfg.repeat_count);
        (void)openmpt_module_set_render_param(
            mod_, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT,
            std::clamp(cfg.stereo_separation_pct, 0, 200));
        (void)openmpt_module_set_render_param(
            mod_, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH,
            cfg.interpolation_filter);
        (void)openmpt_module_set_render_param(
            mod_, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL,
            static_cast<std::int32_t>(std::llround(cfg.master_gain_db * 100.0)));

        if (cfg.subsong >= 0) {
            (void)openmpt_module_select_subsong(mod_, cfg.subsong);
        }

        meta_.num_channels = openmpt_module_get_num_channels(mod_);
        meta_.num_orders   = openmpt_module_get_num_orders(mod_);
        meta_.num_patterns = openmpt_module_get_num_patterns(mod_);
        meta_.num_subsongs = openmpt_module_get_num_subsongs(mod_);
        meta_.duration_sec = openmpt_module_get_duration_seconds(mod_);

        auto take_str = [](const char* s) -> std::string {
            if (!s) return {};
            std::string out(s);
            openmpt_free_string(s);
            return out;
        };
        meta_.title     = take_str(openmpt_module_get_metadata(mod_, "title"));
        meta_.tracker   = take_str(openmpt_module_get_metadata(mod_, "tracker"));
        meta_.type_long = take_str(openmpt_module_get_metadata(mod_, "type_long"));
        return true;
#else
        (void)data;
        (void)size;
        (void)sample_rate_hz;
        (void)cfg;
        return false;
#endif
    }

    bool OpenFromFile(const std::filesystem::path& path,
                      unsigned int sample_rate_hz,
                      const TrackerRenderConfig& cfg = TrackerRenderConfig{}) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
        const auto end_pos = file.tellg();
        if (end_pos <= 0) {
            return false;
        }
        const auto byte_count = static_cast<std::size_t>(end_pos);
        owned_bytes_.resize(byte_count);
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(owned_bytes_.data()),
                       static_cast<std::streamsize>(byte_count))) {
            owned_bytes_.clear();
            return false;
        }
        return OpenFromMemory(owned_bytes_.data(), owned_bytes_.size(), sample_rate_hz, cfg);
    }

    // Renders up to `max_frames` interleaved stereo S32_LE frames directly from the tracker engine.
    // Returns number of stereo frames rendered (0 = end of module).
    std::size_t ReadInterleavedS32(std::int32_t* dst_lr_s32, std::size_t max_frames) {
#if defined(FLAC_AX_HAS_OPENMPT)
        if (!mod_ || !dst_lr_s32 || max_frames == 0U) {
            return 0U;
        }
        if (float_scratch_.size() < max_frames * 2U) {
            float_scratch_.resize(max_frames * 2U);
        }
        const std::size_t rendered = openmpt_module_read_interleaved_float_stereo(
            mod_,
            static_cast<std::int32_t>(sample_rate_),
            max_frames,
            float_scratch_.data());

        constexpr double kScale32 = 2147483647.0;
        const std::size_t samples = rendered * 2U;
        for (std::size_t i = 0U; i < samples; ++i) {
            const double clamped = std::clamp(static_cast<double>(float_scratch_[i]), -1.0, 1.0);
            dst_lr_s32[i] = static_cast<std::int32_t>(std::llround(clamped * kScale32));
        }
        return rendered;
#else
        (void)dst_lr_s32;
        (void)max_frames;
        return 0U;
#endif
    }

    const TrackerMetadata& metadata() const noexcept { return meta_; }

    void Close() noexcept {
#if defined(FLAC_AX_HAS_OPENMPT)
        if (mod_) {
            openmpt_module_destroy(mod_);
            mod_ = nullptr;
        }
#endif
        owned_bytes_.clear();
        float_scratch_.clear();
        meta_ = TrackerMetadata{};
        sample_rate_ = 0U;
    }

private:
#if defined(FLAC_AX_HAS_OPENMPT)
    openmpt_module* mod_{nullptr};
#endif
    unsigned int sample_rate_{0U};
    TrackerMetadata meta_{};
    std::vector<std::uint8_t> owned_bytes_;
    std::vector<float> float_scratch_;
};

} // namespace flac_ax::tracker
