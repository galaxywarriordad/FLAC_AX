// src/dsp/avx2_synth.cpp — Real-time polyphonic synth, SVF filter, and 64-band FFT analyzer
#include "dsp/avx2_synth.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace flac_ax::dsp {

namespace {

inline float MidiNoteToHz(int note, float bend_semitones) noexcept {
    const float shifted = (static_cast<float>(note) - 69.0f) + bend_semitones;
    return 440.0f * std::exp2(shifted * (1.0f / 12.0f));
}

inline float FastSoftClip(float x) noexcept {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline std::int32_t FloatToS32(float sample) noexcept {
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<std::int32_t>(clamped * 2147483520.0f);
}

// In-place radix-2 Cooley-Tukey FFT (N = 1024)
void Radix2Fft(std::array<std::complex<float>, PolySynthEngine::kFftSize>& a) noexcept {
    constexpr std::size_t N = PolySynthEngine::kFftSize;
    std::size_t j = 0;
    for (std::size_t i = 1; i < N; ++i) {
        std::size_t bit = N >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    for (std::size_t len = 2; len <= N; len <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

PolySynthEngine::PolySynthEngine(unsigned int sample_rate_hz) {
    SetSampleRate(sample_rate_hz);
}

void PolySynthEngine::SetSampleRate(unsigned int sample_rate_hz) noexcept {
    sample_rate_ = static_cast<float>(sample_rate_hz > 0U ? sample_rate_hz : 48000U);
    inv_sample_rate_ = 1.0f / sample_rate_;
}

void PolySynthEngine::NoteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
    if (velocity == 0U) {
        NoteOff(note);
        return;
    }

    // Re-trigger same note or allocate free/round-robin voice
    Voice* chosen = nullptr;
    for (auto& v : voices_) {
        if (v.active && v.note == note) {
            chosen = &v;
            break;
        }
    }
    if (chosen == nullptr) {
        for (std::size_t i = 0; i < kMaxVoices; ++i) {
            const std::size_t idx = (next_voice_rr_ + i) % kMaxVoices;
            if (!voices_[idx].active) {
                chosen = &voices_[idx];
                next_voice_rr_ = (idx + 1) % kMaxVoices;
                break;
            }
        }
    }
    if (chosen == nullptr) {
        chosen = &voices_[next_voice_rr_];
        next_voice_rr_ = (next_voice_rr_ + 1) % kMaxVoices;
    }

    chosen->active = true;
    chosen->sustained = false;
    chosen->note = note;
    chosen->velocity = static_cast<float>(velocity) * (1.0f / 127.0f);
    chosen->freq_hz = MidiNoteToHz(note, pitch_bend_semi_);
    chosen->stage = Voice::Stage::Attack;
}

void PolySynthEngine::NoteOff(std::uint8_t note) noexcept {
    for (auto& v : voices_) {
        if (v.active && v.note == note) {
            if (sustain_pedal_) {
                v.sustained = true;
            } else {
                v.stage = Voice::Stage::Release;
            }
        }
    }
}

void PolySynthEngine::ProcessMidiEvent(const midi::MidiEvent& ev) noexcept {
    switch (ev.type) {
        case midi::MidiMsgType::NoteOn:
            NoteOn(ev.data1, ev.data2);
            break;
        case midi::MidiMsgType::NoteOff:
            NoteOff(ev.data1);
            break;
        case midi::MidiMsgType::PitchBend:
            pitch_bend_semi_ = (static_cast<float>(ev.pitch_bend) / 8192.0f) * 2.0f;
            for (auto& v : voices_) {
                if (v.active) {
                    v.freq_hz = MidiNoteToHz(v.note, pitch_bend_semi_);
                }
            }
            break;
        case midi::MidiMsgType::ControlChange: {
            const float norm = static_cast<float>(ev.data2) * (1.0f / 127.0f);
            switch (ev.data1) {
                case 1:
                    mod_wheel_ = norm;
                    break;
                case 64:
                    sustain_pedal_ = (ev.data2 >= 64U);
                    if (!sustain_pedal_) {
                        for (auto& v : voices_) {
                            if (v.active && v.sustained) {
                                v.sustained = false;
                                v.stage = Voice::Stage::Release;
                            }
                        }
                    }
                    break;
                case 70: // K1: Cutoff
                    cutoff_norm_ = 0.05f + 0.90f * norm;
                    break;
                case 71: // K2: Resonance
                    resonance_ = 0.05f + 0.85f * norm;
                    break;
                case 72: // K3: Attack
                    attack_sec_ = 0.002f + 0.60f * (norm * norm);
                    break;
                case 73: // K4: Release
                    release_sec_ = 0.02f + 1.80f * (norm * norm);
                    break;
                case 74: // K5: Sub-oscillator mix
                    sub_mix_ = norm;
                    break;
                case 75: // K6: Harmonic drive
                    drive_ = 1.0f + 3.5f * norm;
                    break;
                case 76: // K7: Stereo chorus spread
                    chorus_depth_ = norm;
                    break;
                case 77: // K8: Master output gain
                    master_gain_ = norm;
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

void PolySynthEngine::RenderInterleavedS32(std::int32_t* out_interleaved_lr, std::size_t frame_count) noexcept {
    const float attack_step = 1.0f / std::max(1.0f, attack_sec_ * sample_rate_);
    const float release_step = 1.0f / std::max(1.0f, release_sec_ * sample_rate_);
    const float decay_step = 1.0f / (0.18f * sample_rate_);
    const float lfo_inc = (4.5f + 5.0f * mod_wheel_) * inv_sample_rate_;

    const std::uint32_t back_idx = 1U - active_snap_idx_.load(std::memory_order_relaxed);
    VisualSnapshot& snap = snapshots_[back_idx];

    for (std::size_t n = 0; n < frame_count; ++n) {
        lfo_phase_ += lfo_inc;
        if (lfo_phase_ >= 1.0f) {
            lfo_phase_ -= 1.0f;
        }
        const float lfo = std::sin(2.0f * std::numbers::pi_v<float> * lfo_phase_);

        float mix_l = 0.0f;
        float mix_r = 0.0f;
        std::uint32_t active_count = 0;

        for (std::size_t vi = 0; vi < kMaxVoices; ++vi) {
            Voice& v = voices_[vi];
            if (!v.active) {
                continue;
            }

            switch (v.stage) {
                case Voice::Stage::Attack:
                    v.env += attack_step;
                    if (v.env >= 1.0f) {
                        v.env = 1.0f;
                        v.stage = Voice::Stage::Decay;
                    }
                    break;
                case Voice::Stage::Decay:
                    v.env -= decay_step;
                    if (v.env <= 0.75f) {
                        v.env = 0.75f;
                        v.stage = Voice::Stage::Sustain;
                    }
                    break;
                case Voice::Stage::Sustain:
                    break;
                case Voice::Stage::Release:
                    v.env -= release_step;
                    if (v.env <= 0.0f) {
                        v.env = 0.0f;
                        v.active = false;
                        v.stage = Voice::Stage::Off;
                        continue;
                    }
                    break;
                case Voice::Stage::Off:
                    v.active = false;
                    continue;
            }

            ++active_count;
            const float vibrato = 1.0f + (0.008f * mod_wheel_ * lfo);
            const float dt = v.freq_hz * vibrato * inv_sample_rate_;

            v.phase_main += dt;
            if (v.phase_main >= 1.0f) {
                v.phase_main -= 1.0f;
            }
            v.phase_sub += dt * 0.5f;
            if (v.phase_sub >= 1.0f) {
                v.phase_sub -= 1.0f;
            }

            // Band-warmed saw + sub square
            const float saw = (2.0f * v.phase_main) - 1.0f;
            const float sub = (v.phase_sub < 0.5f) ? 1.0f : -1.0f;
            const float osc = ((1.0f - 0.35f * sub_mix_) * saw) + (0.5f * sub_mix_ * sub);

            const float pan = 0.5f + (((vi & 1U) ? 0.35f : -0.35f) * chorus_depth_);
            const float amp = osc * v.env * v.velocity * 0.28f;
            mix_l += amp * (1.0f - pan);
            mix_r += amp * pan;
        }

        // 2-pole Chamberlin State-Variable Low-Pass Filter + Soft Saturation
        const float mod_cutoff = std::clamp(cutoff_norm_ + 0.15f * mod_wheel_ * lfo, 0.04f, 0.96f);
        const float f = 2.0f * std::sin(std::numbers::pi_v<float> * (mod_cutoff * 0.42f));
        const float q = 1.0f - (0.88f * resonance_);

        const float driven_l = FastSoftClip(mix_l * drive_);
        const float driven_r = FastSoftClip(mix_r * drive_);

        const float hp_l = driven_l - svf_lp_l_ - q * svf_bp_l_;
        svf_bp_l_ += f * hp_l;
        svf_lp_l_ += f * svf_bp_l_;

        const float hp_r = driven_r - svf_lp_r_ - q * svf_bp_r_;
        svf_bp_r_ += f * hp_r;
        svf_lp_r_ += f * svf_bp_r_;

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


