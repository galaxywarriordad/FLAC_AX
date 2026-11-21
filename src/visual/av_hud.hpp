        const float out_l = FastSoftClip(svf_lp_l_ * master_gain_);
        const float out_r = FastSoftClip(svf_lp_r_ * master_gain_);

        out_interleaved_lr[n * 2 + 0] = FloatToS32(out_l);
        out_interleaved_lr[n * 2 + 1] = FloatToS32(out_r);

        // Feed visual ring & oscilloscope
        const std::size_t scope_idx = (fft_write_pos_ + n) % VisualSnapshot::kScopeSamples;
        snap.scope_l[scope_idx] = out_l;
        snap.scope_r[scope_idx] = out_r;
        snap.peak_l = std::max(snap.peak_l * 0.995f, std::fabs(out_l));
        snap.peak_r = std::max(snap.peak_r * 0.995f, std::fabs(out_r));
        snap.active_voices = active_count;

        fft_ring_[fft_write_pos_] = 0.5f * (out_l + out_r);
        fft_write_pos_ = (fft_write_pos_ + 1) % kFftSize;
        if (++frames_since_fft_ >= 256) {
            frames_since_fft_ = 0;
            ComputeFftSnapshot();
        }
    }
}

void PolySynthEngine::ComputeFftSnapshot() noexcept {
    std::array<std::complex<float>, kFftSize> fft_buf{};
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const std::size_t idx = (fft_write_pos_ + i) % kFftSize;
        const float hann = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> *
                                                   static_cast<float>(i) / static_cast<float>(kFftSize - 1)));
        fft_buf[i] = std::complex<float>(fft_ring_[idx] * hann, 0.0f);
    }

    Radix2Fft(fft_buf);

    const std::uint32_t back_idx = 1U - active_snap_idx_.load(std::memory_order_relaxed);
    VisualSnapshot& snap = snapshots_[back_idx];

    constexpr std::size_t kHalfBins = kFftSize / 2;
    for (std::size_t b = 0; b < VisualSnapshot::kSpectrumBins; ++b) {
        const float t0 = static_cast<float>(b) / static_cast<float>(VisualSnapshot::kSpectrumBins);
        const float t1 = static_cast<float>(b + 1) / static_cast<float>(VisualSnapshot::kSpectrumBins);
        const std::size_t k0 = std::max<std::size_t>(1, static_cast<std::size_t>(std::pow(static_cast<float>(kHalfBins), t0)));
        const std::size_t k1 = std::max<std::size_t>(k0 + 1, static_cast<std::size_t>(std::pow(static_cast<float>(kHalfBins), t1)));

        float mag_sum = 0.0f;
        for (std::size_t k = k0; k < std::min(k1, kHalfBins); ++k) {
            mag_sum += std::abs(fft_buf[k]);
        }
        const float avg_mag = mag_sum / static_cast<float>(std::max<std::size_t>(1, k1 - k0));
        const float db = 20.0f * std::log10((avg_mag / static_cast<float>(kFftSize)) + 1e-6f);
        const float norm = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
        snap.spectrum[b] = 0.65f * snap.spectrum[b] + 0.35f * norm;
    }

    active_snap_idx_.store(back_idx, std::memory_order_release);
}

VisualSnapshot PolySynthEngine::ReadVisualSnapshot() const noexcept {
    const std::uint32_t idx = active_snap_idx_.load(std::memory_order_acquire);
    return snapshots_[idx];
}

} // namespace flac_ax::dsp
