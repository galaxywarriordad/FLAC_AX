// src/dsp/tape_delay_dsp.hpp — Zero-allocation stereo damped delay DSP block
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace flac_ax::dsp {

class TapeDelayDsp {
public:
    // Up to 1.0s at 192 kHz
    static constexpr std::size_t kMaxDelaySamples = 192000;

    explicit TapeDelayDsp(unsigned int sample_rate_hz = 48000) noexcept {
        SetSampleRate(sample_rate_hz);
        SetDelayMs(240.0f);
    }

    void SetSampleRate(unsigned int sample_rate_hz) noexcept {
        sample_rate_ = static_cast<float>(sample_rate_hz > 0U ? sample_rate_hz : 48000U);
        SetDelayMs(delay_ms_);
    }

    void SetDelayMs(float ms) noexcept {
        delay_ms_ = std::clamp(ms, 5.0f, 950.0f);
        const float samples = (delay_ms_ * 0.001f) * sample_rate_;
        target_delay_samples_ = std::clamp(samples, 1.0f, static_cast<float>(kMaxDelaySamples - 2U));
        if (smooth_delay_samples_ <= 0.0f) {
            smooth_delay_samples_ = target_delay_samples_;
        }
    }

    void SetFeedback(float norm_0_to_1) noexcept {
        feedback_ = std::clamp(norm_0_to_1, 0.0f, 0.88f);
    }

    void SetToneDamping(float norm_0_to_1) noexcept {
        // Higher damping = more high-frequency roll-off per repeat
        damp_ = std::clamp(norm_0_to_1, 0.0f, 1.0f) * 0.80f;
    }

    void SetMix(float wet_0_to_1) noexcept {
        wet_ = std::clamp(wet_0_to_1, 0.0f, 1.0f);
    }

    void ProcessStereo(float& io_l, float& io_r) noexcept {
        // Smooth delay time changes to avoid zipper/pitch jumps
        smooth_delay_samples_ += 0.002f * (target_delay_samples_ - smooth_delay_samples_);

        const float tap_l = ReadInterpolated(buf_l_, write_pos_, smooth_delay_samples_);
        // Slight stereo offset (+11% or min 8 samples) for wider echo image
        const float offset_r = std::clamp(smooth_delay_samples_ * 1.11f, 8.0f,
                                          static_cast<float>(kMaxDelaySamples - 2U));
        const float tap_r = ReadInterpolated(buf_r_, write_pos_, offset_r);

        lp_fb_l_ = (1.0f - damp_) * tap_l + damp_ * lp_fb_l_;
        lp_fb_r_ = (1.0f - damp_) * tap_r + damp_ * lp_fb_r_;

        const float fb_l = SoftSat(io_l + lp_fb_l_ * feedback_);
        const float fb_r = SoftSat(io_r + lp_fb_r_ * feedback_);

        buf_l_[write_pos_] = fb_l;
        buf_r_[write_pos_] = fb_r;
        write_pos_ = (write_pos_ + 1U) % kMaxDelaySamples;

        io_l = (1.0f - wet_) * io_l + wet_ * tap_l;
        io_r = (1.0f - wet_) * io_r + wet_ * tap_r;
    }

private:
    static float SoftSat(float x) noexcept {
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    static float ReadInterpolated(const std::array<float, kMaxDelaySamples>& buf,
                                  std::size_t write_pos,
                                  float delay_samples) noexcept {
        const std::size_t int_delay = static_cast<std::size_t>(delay_samples);
        const float frac = delay_samples - static_cast<float>(int_delay);

        const std::size_t idx0 = (write_pos + kMaxDelaySamples - int_delay) % kMaxDelaySamples;
        const std::size_t idx1 = (idx0 + kMaxDelaySamples - 1U) % kMaxDelaySamples;
        return buf[idx0] + frac * (buf[idx1] - buf[idx0]);
    }

    float sample_rate_{48000.0f};
    float delay_ms_{240.0f};
    float target_delay_samples_{11520.0f};
    float smooth_delay_samples_{0.0f};
    float feedback_{0.40f};
    float damp_{0.35f};
    float wet_{0.15f};

    float lp_fb_l_{0.0f};
    float lp_fb_r_{0.0f};
    std::size_t write_pos_{0};
    std::array<float, kMaxDelaySamples> buf_l_{};
    std::array<float, kMaxDelaySamples> buf_r_{};
};

} // namespace flac_ax::dsp
