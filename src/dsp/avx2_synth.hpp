// src/dsp/avx2_synth.hpp — 16-voice polyphonic synth, 8-knob tactile mixer & 64-band FFT visual tap
#pragma once

#include "midi/mpk_mini3.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace flac_ax::dsp {

struct VisualSnapshot {
    static constexpr std::size_t kScopeSamples = 256;
    static constexpr std::size_t kSpectrumBins = 64;

    std::array<float, kScopeSamples> scope_l{};
    std::array<float, kScopeSamples> scope_r{};
    std::array<float, kSpectrumBins> spectrum{}; // Normalized 0.0..1.0 log-spaced magnitudes
    float peak_l{0.0f};
    float peak_r{0.0f};
    std::uint32_t active_voices{0};
};

class PolySynthEngine {
public:
    static constexpr std::size_t kMaxVoices = 16;
    static constexpr std::size_t kFftSize = 1024;

    explicit PolySynthEngine(unsigned int sample_rate_hz = 48000);

    void SetSampleRate(unsigned int sample_rate_hz) noexcept;
    void ProcessMidiEvent(const midi::MidiEvent& ev) noexcept;

    // Renders `frame_count` stereo S32_LE frames (96-frame period = 2.00 ms @ 48 kHz)
    // and updates the lock-free visual snapshot ring every 256 frames.
    void RenderInterleavedS32(std::int32_t* out_interleaved_lr, std::size_t frame_count) noexcept;

    // Thread-safe double-buffered snapshot read by Core 1 (GTX 980 Ti OpenGL HUD)
    VisualSnapshot ReadVisualSnapshot() const noexcept;

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

    void NoteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void NoteOff(std::uint8_t note) noexcept;
    void ComputeFftSnapshot() noexcept;

    float sample_rate_{48000.0f};
    float inv_sample_rate_{1.0f / 48000.0f};

    std::array<Voice, kMaxVoices> voices_{};
    std::size_t next_voice_rr_{0};

    // MPK Mini MK3 K1..K8 parameters (CC 70..77) + Thumbstick + Sustain
    float cutoff_norm_{0.75f};     // K1 (CC 70): Low-pass filter cutoff
    float resonance_{0.20f};       // K2 (CC 71): Filter resonance
    float attack_sec_{0.008f};     // K3 (CC 72): Envelope attack
    float release_sec_{0.25f};     // K4 (CC 73): Envelope release
    float sub_mix_{0.45f};         // K5 (CC 74): Sub-oscillator octave mix
    float drive_{1.2f};            // K6 (CC 75): Warm harmonic saturation
    float chorus_depth_{0.25f};    // K7 (CC 76): Stereo width / detune
    float master_gain_{0.70f};     // K8 (CC 77): Master bus level
    float pitch_bend_semi_{0.0f};  // Thumbstick X (-2..+2 semitones)
    float mod_wheel_{0.0f};        // Thumbstick Y (CC 1)
    bool sustain_pedal_{false};    // CC 64

    // State-variable filter state (L/R)
    float svf_lp_l_{0.0f}, svf_bp_l_{0.0f};
    float svf_lp_r_{0.0f}, svf_bp_r_{0.0f};
    float lfo_phase_{0.0f};

    // Sliding 1024-sample mono ring for real-time FFT + 256-sample stereo scope
    std::array<float, kFftSize> fft_ring_{};
    std::size_t fft_write_pos_{0};
    std::size_t frames_since_fft_{0};

    alignas(64) std::array<VisualSnapshot, 2> snapshots_{};
    alignas(64) std::atomic<std::uint32_t> active_snap_idx_{0};
};

} // namespace flac_ax::dsp
