// src/dsp/stereo_reverb_dsp.hpp — Zero-allocation stereo feedback delay / reverb DSP block
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace flac_ax::dsp {

class StereoReverbDsp {
public:
    // Supports up to 192 kHz with generous tap headroom and zero heap allocations
    static constexpr std::size_t kMaxDelaySamples = 16384;
    static constexpr std::size_t kNumCombs = 4;

    explicit StereoReverbDsp(unsigned int sample_rate_hz = 48000) noexcept {
        SetSampleRate(sample_rate_hz);
    }

    void SetSampleRate(unsigned int sample_rate_hz) noexcept {
        const float sr = static_cast<float>(sample_rate_hz > 0U ? sample_rate_hz : 48000U);
        const float scale = sr / 48000.0f;

        // Prime-ish tap lengths scaled to sample rate, clamped to kMaxDelaySamples - 1
        static constexpr std::array<std::size_t, kNumCombs> kBaseTapsL{1557, 1617, 1491, 1422};
        static constexpr std::array<std::size_t, kNumCombs> kBaseTapsR{1579, 1639, 1513, 1444};

        for (std::size_t i = 0; i < kNumCombs; ++i) {
            tap_len_l_[i] = std::clamp<std::size_t>(
                static_cast<std::size_t>(static_cast<float>(kBaseTapsL[i]) * scale),
                32U, kMaxDelaySamples - 1U);
            tap_len_r_[i] = std::clamp<std::size_t>(
                static_cast<std::size_t>(static_cast<float>(kBaseTapsR[i]) * scale),
                32U, kMaxDelaySamples - 1U);
        }
    }

    void SetRoomSize(float norm_0_to_1) noexcept {
        feedback_ = 0.20f + 0.68f * std::clamp(norm_0_to_1, 0.0f, 1.0f);
    }

    void SetDamping(float norm_0_to_1) noexcept {
        damp_ = std::clamp(norm_0_to_1, 0.0f, 1.0f) * 0.75f;
    }

    void SetMix(float wet_0_to_1) noexcept {
        wet_ = std::clamp(wet_0_to_1, 0.0f, 1.0f);
    }

    void ProcessStereo(float& io_l, float& io_r) noexcept {
        const float mono_in = 0.5f * (io_l + io_r);
        float wet_l = 0.0f;
        float wet_r = 0.0f;

        for (std::size_t i = 0; i < kNumCombs; ++i) {
            // Left comb
            const std::size_t r_idx_l = (write_idx_l_[i] + kMaxDelaySamples - tap_len_l_[i]) % kMaxDelaySamples;
            const float delayed_l = comb_l_[i][r_idx_l];
            lp_l_[i] = (1.0f - damp_) * delayed_l + damp_ * lp_l_[i];
            comb_l_[i][write_idx_l_[i]] = mono_in + lp_l_[i] * feedback_;
            write_idx_l_[i] = (write_idx_l_[i] + 1U) % kMaxDelaySamples;
            wet_l += delayed_l;

            // Right comb
            const std::size_t r_idx_r = (write_idx_r_[i] + kMaxDelaySamples - tap_len_r_[i]) % kMaxDelaySamples;
            const float delayed_r = comb_r_[i][r_idx_r];
            lp_r_[i] = (1.0f - damp_) * delayed_r + damp_ * lp_r_[i];
            comb_r_[i][write_idx_r_[i]] = mono_in + lp_r_[i] * feedback_;
            write_idx_r_[i] = (write_idx_r_[i] + 1U) % kMaxDelaySamples;
            wet_r += delayed_r;
        }

        wet_l *= 0.25f;
        wet_r *= 0.25f;

        io_l = (1.0f - wet_) * io_l + wet_ * wet_l;
        io_r = (1.0f - wet_) * io_r + wet_ * wet_r;
    }

private:
    float feedback_{0.55f};
    float damp_{0.30f};
    float wet_{0.20f};

    std::array<std::array<float, kMaxDelaySamples>, kNumCombs> comb_l_{};
    std::array<std::array<float, kMaxDelaySamples>, kNumCombs> comb_r_{};
    std::array<std::size_t, kNumCombs> tap_len_l_{};
    std::array<std::size_t, kNumCombs> tap_len_r_{};
    std::array<std::size_t, kNumCombs> write_idx_l_{};
    std::array<std::size_t, kNumCombs> write_idx_r_{};
    std::array<float, kNumCombs> lp_l_{};
    std::array<float, kNumCombs> lp_r_{};
};

} // namespace flac_ax::dsp
