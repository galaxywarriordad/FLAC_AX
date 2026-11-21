// src/dsp/full_feature_synth.hpp — Zero-dependency full-interface test synth trio for ax_av_studio
// Integrates FullFeatureSynthEngine + StereoReverbDsp + TapeDelayDsp with zero RT allocations
#pragma once

#include "dsp/avx2_synth.hpp"      // VisualSnapshot
#include "dsp/stereo_reverb_dsp.hpp"
#include "dsp/tape_delay_dsp.hpp"
#include "midi/mpk_mini3.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace flac_ax::dsp {

class FullFeatureSynthEngine {
public:
    static constexpr std::size_t kMaxVoices = 16;
    static constexpr std::size_t kMaxDrumVoices = 8;
    static constexpr std::size_t kFftSize = 1024;
    static constexpr std::size_t kHalfBins = kFftSize / 2;

    explicit FullFeatureSynthEngine(unsigned int sample_rate_hz = 48000)
        : reverb_(sample_rate_hz), delay_(sample_rate_hz) {
        InitLookupTables();
        SetSampleRate(sample_rate_hz);
        reverb_.SetRoomSize(0.55f);
        reverb_.SetDamping(0.30f);
        reverb_.SetMix(0.18f);
        delay_.SetDelayMs(240.0f);
        delay_.SetFeedback(0.42f);
        delay_.SetToneDamping(0.35f);
        delay_.SetMix(0.15f);
    }

    void SetSampleRate(unsigned int sample_rate_hz) noexcept {
        sample_rate_ = static_cast<float>(sample_rate_hz > 0U ? sample_rate_hz : 48000U);
        inv_sample_rate_ = 1.0f / sample_rate_;
        reverb_.SetSampleRate(sample_rate_hz);
        delay_.SetSampleRate(sample_rate_hz);
    }

    StereoReverbDsp& reverb() noexcept { return reverb_; }
    TapeDelayDsp& delay() noexcept { return delay_; }

    void ProcessMidiEvent(const midi::MidiEvent& ev) noexcept {
        switch (ev.type) {
            case midi::MidiMsgType::NoteOn:
                if (ev.data2 == 0U) {
                    NoteOff(ev.channel, ev.data1);
                } else if (ev.channel == 9U || (ev.data1 >= 36U && ev.data1 <= 51U && ev.channel == 1U)) {
                    TriggerDrumPad(ev.data1, ev.data2);
                } else {
                    NoteOn(ev.data1, ev.data2);
                }
                break;

            case midi::MidiMsgType::NoteOff:
                NoteOff(ev.channel, ev.data1);
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
                    case 1U: // Thumbstick Y / Mod Wheel
                        mod_wheel_ = norm;
                        break;
                    case 64U: // Sustain pedal
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
                    case 70U: // K1: Cutoff
                        cutoff_norm_ = 0.05f + 0.90f * norm;
                        break;
                    case 71U: // K2: Resonance
                        resonance_ = 0.05f + 0.85f * norm;
                        break;
                    case 72U: // K3: Attack
                        attack_sec_ = 0.002f + 0.60f * (norm * norm);
                        break;
                    case 73U: // K4: Release
                        release_sec_ = 0.02f + 1.80f * (norm * norm);
                        break;
                    case 74U: // K5: Sub-oscillator mix
                        sub_mix_ = norm;
                        break;
                    case 75U: // K6: Harmonic drive
                        drive_ = 1.0f + 3.5f * norm;
                        break;
                    case 76U: // K7: Stereo chorus spread + subtle reverb blend
                        chorus_depth_ = norm;
                        reverb_.SetMix(0.08f + 0.32f * norm);
                        break;
                    case 77U: // K8: Master output gain
                        master_gain_ = norm;
                        break;
                    case 78U: // Optional CC 78: Reverb mix & room size
                        reverb_.SetMix(norm * 0.75f);
                        reverb_.SetRoomSize(norm);
                        break;
                    case 79U: // Optional CC 79: Tape delay mix & feedback
                        delay_.SetMix(norm * 0.75f);
                        delay_.SetFeedback(0.15f + 0.65f * norm);
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

    // Zero-allocation real-time audio + reverb + delay + scope + 64-band FFT tap
    void RenderInterleavedS32(std::int32_t* out_interleaved_lr, std::size_t frame_count) noexcept {
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

            // 16 melodic synth voices
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

                const float saw = (2.0f * v.phase_main) - 1.0f;
                const float sub = (v.phase_sub < 0.5f) ? 1.0f : -1.0f;
                const float osc = ((1.0f - 0.35f * sub_mix_) * saw) + (0.5f * sub_mix_ * sub);

                const float pan = 0.5f + (((vi & 1U) ? 0.35f : -0.35f) * chorus_depth_);
                const float amp = osc * v.env * v.velocity * 0.25f;
                mix_l += amp * (1.0f - pan);
                mix_r += amp * pan;
            }

            // 8 polyphonic drum/pad voices
            for (std::size_t di = 0; di < kMaxDrumVoices; ++di) {
                DrumVoice& d = drums_[di];
                if (!d.active) {
                    continue;
                }
                ++active_count;

                const float cur_freq = d.base_freq + d.pitch_env * 140.0f;
                d.phase += cur_freq * inv_sample_rate_;
                if (d.phase >= 1.0f) {
                    d.phase -= 1.0f;
                }
                d.pitch_env *= d.pitch_decay;

                const float tone = std::sin(2.0f * std::numbers::pi_v<float> * d.phase) * d.amp_env;
                const float noise = NextNoiseSample() * d.noise_env * d.noise_mix;
                d.amp_env *= d.amp_decay;
                d.noise_env *= d.noise_decay;

                if (d.amp_env < 0.0005f && d.noise_env < 0.0005f) {
                    d.active = false;
                    continue;
                }

                const float sample = (tone + noise) * d.vel * 0.45f;
                mix_l += sample * (1.0f - d.pan);
                mix_r += sample * d.pan;
            }

            // Stereo 2-pole SVF low-pass + soft saturation
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

            // DSP trio spatial & echo chain: StereoReverbDsp -> TapeDelayDsp -> Master Gain
            float proc_l = svf_lp_l_;
            float proc_r = svf_lp_r_;
            reverb_.ProcessStereo(proc_l, proc_r);
            delay_.ProcessStereo(proc_l, proc_r);

            const float out_l = FastSoftClip(proc_l * master_gain_);
            const float out_r = FastSoftClip(proc_r * master_gain_);

            out_interleaved_lr[n * 2 + 0] = FloatToS32(out_l);
            out_interleaved_lr[n * 2 + 1] = FloatToS32(out_r);

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

    VisualSnapshot ReadVisualSnapshot() const noexcept {
        return snapshots_[active_snap_idx_.load(std::memory_order_acquire)];
    }

private:
    struct Voice {
        bool active{false};
        bool sustained{false};
        std::uint8_t note{60};
        float velocity{0.0f};
        float phase_main{0.0f};
        float phase_sub{0.0f};
        float freq_hz{440.0f};
        float env{0.0f};
        enum class Stage : std::uint8_t { Off, Attack, Decay, Sustain, Release } stage{Stage::Off};
    };

    struct DrumVoice {
        bool active{false};
        float phase{0.0f};
        float base_freq{65.0f};
        float pitch_env{0.0f};
        float pitch_decay{0.9992f};
        float amp_env{0.0f};
        float amp_decay{0.9996f};
        float noise_env{0.0f};
        float noise_decay{0.9985f};
        float noise_mix{0.0f};
        float vel{0.0f};
        float pan{0.5f};
    };

    struct BinRange {
        std::size_t k0{1};
        std::size_t k1{2};
        float inv_count{1.0f};
    };

    static float MidiNoteToHz(int note, float bend_semitones) noexcept {
        const float shifted = (static_cast<float>(note) - 69.0f) + bend_semitones;
        return 440.0f * std::exp2(shifted * (1.0f / 12.0f));
    }

    static float FastSoftClip(float x) noexcept {
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    static std::int32_t FloatToS32(float sample) noexcept {
        const float clamped = std::clamp(sample, -1.0f, 1.0f);
        return static_cast<std::int32_t>(clamped * 2147483520.0f);
    }

    float NextNoiseSample() noexcept {
        std::uint32_t x = noise_rng_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        noise_rng_ = (x == 0U) ? 0xA341316CU : x;
        return (static_cast<float>(noise_rng_) * (1.0f / 2147483648.0f)) - 1.0f;
    }

    void InitLookupTables() noexcept {
        for (std::size_t i = 0; i < kFftSize; ++i) {
            hann_window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> *
                                                      static_cast<float>(i) /
                                                      static_cast<float>(kFftSize - 1)));
        }
        for (std::size_t b = 0; b < VisualSnapshot::kSpectrumBins; ++b) {
            const float t0 = static_cast<float>(b) / static_cast<float>(VisualSnapshot::kSpectrumBins);
            const float t1 = static_cast<float>(b + 1) / static_cast<float>(VisualSnapshot::kSpectrumBins);
            const std::size_t k0 = std::max<std::size_t>(
                1, static_cast<std::size_t>(std::pow(static_cast<float>(kHalfBins), t0)));
            const std::size_t k1 = std::min<std::size_t>(
                kHalfBins, std::max<std::size_t>(
                    k0 + 1, static_cast<std::size_t>(std::pow(static_cast<float>(kHalfBins), t1))));
            bin_ranges_[b] = BinRange{
                .k0 = k0,
                .k1 = k1,
                .inv_count = 1.0f / static_cast<float>(std::max<std::size_t>(1, k1 - k0)),
            };
        }
    }

    void NoteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
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

    void NoteOff(std::uint8_t ch, std::uint8_t note) noexcept {
        if (ch == 9U) {
            return;
        }
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

    void TriggerDrumPad(std::uint8_t note, std::uint8_t velocity) noexcept {
        DrumVoice& d = drums_[next_drum_rr_];
        next_drum_rr_ = (next_drum_rr_ + 1) % kMaxDrumVoices;

        const std::size_t pad_idx = (note >= 36U && note < 52U) ? static_cast<std::size_t>(note - 36U) : 0U;
        const float vel = static_cast<float>(velocity) * (1.0f / 127.0f);
        const bool is_kick = (pad_idx % 4U) == 0U;
        const bool is_snare = (pad_idx % 4U) == 1U;

        d.active = true;
        d.phase = 0.0f;
        d.vel = vel;
        d.pan = 0.25f + 0.50f * (static_cast<float>(pad_idx % 8U) / 7.0f);

        if (is_kick) {
            d.base_freq = 52.0f + static_cast<float>(pad_idx) * 2.0f;
            d.pitch_env = 1.0f;
            d.pitch_decay = 0.9985f;
            d.amp_env = 1.0f;
            d.amp_decay = 0.99955f;
            d.noise_env = 0.10f;
            d.noise_decay = 0.9950f;
            d.noise_mix = 0.15f;
        } else if (is_snare) {
            d.base_freq = 185.0f + static_cast<float>(pad_idx) * 6.0f;
            d.pitch_env = 0.35f;
            d.pitch_decay = 0.9970f;
            d.amp_env = 0.85f;
            d.amp_decay = 0.99910f;
            d.noise_env = 1.0f;
            d.noise_decay = 0.99860f;
            d.noise_mix = 0.85f;
        } else {
            d.base_freq = 440.0f + static_cast<float>(pad_idx) * 95.0f;
            d.pitch_env = 0.10f;
            d.pitch_decay = 0.9950f;
            d.amp_env = 0.50f;
            d.amp_decay = 0.99820f;
            d.noise_env = 0.95f;
            d.noise_decay = 0.99720f;
            d.noise_mix = 0.90f;
        }
    }

    static void Radix2Fft(std::array<std::complex<float>, kFftSize>& a) noexcept {
        std::size_t j = 0;
        for (std::size_t i = 1; i < kFftSize; ++i) {
            std::size_t bit = kFftSize >> 1;
            while ((j & bit) != 0) {
                j ^= bit;
                bit >>= 1;
            }
            j ^= bit;
            if (i < j) {
                std::swap(a[i], a[j]);
            }
        }

        for (std::size_t len = 2; len <= kFftSize; len <<= 1) {
            const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
            const std::complex<float> wlen(std::cos(angle), std::sin(angle));
            for (std::size_t i = 0; i < kFftSize; i += len) {
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

    void ComputeFftSnapshot() noexcept {
        for (std::size_t i = 0; i < kFftSize; ++i) {
            const std::size_t idx = (fft_write_pos_ + i) % kFftSize;
            fft_work_[i] = std::complex<float>(fft_ring_[idx] * hann_window_[i], 0.0f);
        }

        Radix2Fft(fft_work_);

        const std::uint32_t back_idx = 1U - active_snap_idx_.load(std::memory_order_relaxed);
        VisualSnapshot& snap = snapshots_[back_idx];

        for (std::size_t b = 0; b < VisualSnapshot::kSpectrumBins; ++b) {
            const BinRange& r = bin_ranges_[b];
            float mag_sum = 0.0f;
            for (std::size_t k = r.k0; k < r.k1; ++k) {
                mag_sum += std::abs(fft_work_[k]);
            }
            const float avg_mag = mag_sum * r.inv_count;
            const float db = 20.0f * std::log10((avg_mag / static_cast<float>(kFftSize)) + 1e-6f);
            const float norm = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
            snap.spectrum[b] = 0.65f * snap.spectrum[b] + 0.35f * norm;
        }

        active_snap_idx_.store(back_idx, std::memory_order_release);
    }

    float sample_rate_{48000.0f};
    float inv_sample_rate_{1.0f / 48000.0f};

    std::array<Voice, kMaxVoices> voices_{};
    std::size_t next_voice_rr_{0};

    std::array<DrumVoice, kMaxDrumVoices> drums_{};
    std::size_t next_drum_rr_{0};
    std::uint32_t noise_rng_{0x9E3779B9U};

    StereoReverbDsp reverb_;
    TapeDelayDsp delay_;

    // K1..K8 (CC 70..77) + Thumbstick + Sustain
    float cutoff_norm_{0.75f};
    float resonance_{0.20f};
    float attack_sec_{0.008f};
    float release_sec_{0.25f};
    float sub_mix_{0.45f};
    float drive_{1.2f};
    float chorus_depth_{0.25f};
    float master_gain_{0.70f};
    float pitch_bend_semi_{0.0f};
    float mod_wheel_{0.0f};
    bool sustain_pedal_{false};

    // State-variable filter state
    float svf_lp_l_{0.0f}, svf_bp_l_{0.0f};
    float svf_lp_r_{0.0f}, svf_bp_r_{0.0f};
    float lfo_phase_{0.0f};

    // Precomputed FFT & spectrum tables
    std::array<float, kFftSize> hann_window_{};
    std::array<BinRange, VisualSnapshot::kSpectrumBins> bin_ranges_{};
    std::array<std::complex<float>, kFftSize> fft_work_{};

    // Sliding 1024-sample mono ring for FFT + 256-sample stereo scope
    std::array<float, kFftSize> fft_ring_{};
    std::size_t fft_write_pos_{0};
    std::size_t frames_since_fft_{0};

    alignas(64) std::array<VisualSnapshot, 2> snapshots_{};
    alignas(64) std::atomic<std::uint32_t> active_snap_idx_{0};
};

} // namespace flac_ax::dsp
