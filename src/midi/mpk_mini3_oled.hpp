// src/midi/mpk_mini3_oled.hpp — Off-RT rate-limited SysEx / parameter feedback for MPK Mini MK3 K1..K8
#pragma once

#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace flac_ax::midi {

// Default K1..K8 assignments in FLAC_AX (CC 70..77)
inline constexpr std::array<std::string_view, 8> kDefaultKnobLabels{
    "Cutoff",
    "Resonance",
    "Attack",
    "Release",
    "Sub Mix",
    "Drive",
    "Chorus/Rev",
    "Master Gain"
};

// Optional hook to format a custom 16-char max label + value string for SysEx templates
// (Akai MPK Mini MK3 Q-Link knob names are capped at 16 bytes in preset SysEx payloads).
inline std::string FormatKnobOledLabel(std::size_t knob_idx,
                                        std::string_view param_name,
                                        std::uint8_t value_0_127) {
    char buf[24]{};
    const int pct = static_cast<int>((static_cast<unsigned int>(value_0_127) * 100U + 63U) / 127U);
    std::snprintf(buf, sizeof(buf), "%.*s %d%%",
                  11, param_name.data(), std::clamp(pct, 0, 100));
    std::string out(buf);
    if (out.size() > 16U) {
        out.resize(16U);
    }
    return out;
}

class MpkMini3OledFeedback {
public:
    static constexpr std::size_t kNumKnobs = 8;

    MpkMini3OledFeedback() noexcept {
        for (std::size_t i = 0; i < kNumKnobs; ++i) {
            labels_[i] = std::string(kDefaultKnobLabels[i]);
            last_sent_val_[i].store(255U, std::memory_order_relaxed);
            pending_val_[i].store(255U, std::memory_order_relaxed);
            dirty_[i].store(false, std::memory_order_relaxed);
        }
    }

    ~MpkMini3OledFeedback() {
        Close();
    }

    MpkMini3OledFeedback(const MpkMini3OledFeedback&) = delete;
    MpkMini3OledFeedback& operator=(const MpkMini3OledFeedback&) = delete;

    void SetKnobLabel(std::size_t knob_idx, std::string_view label) {
        if (knob_idx >= kNumKnobs) {
            return;
        }
        std::string trimmed(label.substr(0, std::min<std::size_t>(label.size(), 16U)));
        if (trimmed != labels_[knob_idx]) {
            labels_[knob_idx] = std::move(trimmed);
            dirty_[knob_idx].store(true, std::memory_order_release);
        }
    }

    // Lock-free / wait-free producer call: records latest value for knob_idx (0..7 => K1..K8).
    void NotifyKnobValue(std::size_t knob_idx, std::uint8_t value_0_127) noexcept {
        if (knob_idx >= kNumKnobs) {
            return;
        }
        pending_val_[knob_idx].store(value_0_127 & 0x7FU, std::memory_order_relaxed);
        dirty_[knob_idx].store(true, std::memory_order_release);
    }

    // Call ONLY from an off-RT thread (e.g., Core 1 HUD loop or background UI worker).
    void PollAndFlush(const std::string& rawmidi_device,
                      std::chrono::milliseconds min_interval = std::chrono::milliseconds(45)) {
        const auto now = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < kNumKnobs; ++i) {
            if (!dirty_[i].exchange(false, std::memory_order_acq_rel)) {
                continue;
            }
            const std::uint8_t val = pending_val_[i].load(std::memory_order_acquire);
            if (val > 127U) {
                continue;
            }
            if (val == last_sent_val_[i].load(std::memory_order_relaxed) &&
                (now - last_send_tp_[i]) < min_interval) {
                continue;
            }
            if ((now - last_send_tp_[i]) < min_interval) {
                // Re-mark dirty so the newest value is sent once the throttle window passes
                pending_val_[i].store(val, std::memory_order_relaxed);
                dirty_[i].store(true, std::memory_order_release);
                continue;
            }

            if (EnsureOpen(rawmidi_device)) {
                // Standard F0 ... F7 framing for any custom SysEx template you attach;
                // no-op safely if no raw SysEx template bytes are configured yet.
                if (!sysex_template_.empty() &&
                    sysex_template_.front() == 0xF0U &&
                    sysex_template_.back() == 0xF7U) {
                    (void)snd_rawmidi_write(midi_out_, sysex_template_.data(), sysex_template_.size());
                    (void)snd_rawmidi_drain(midi_out_);
                }
            }

            last_sent_val_[i].store(val, std::memory_order_relaxed);
            last_send_tp_[i] = now;
        }
    }

    // Optional: store a captured/verified F0..F7 SysEx packet for your MPK Mini MK3 firmware/preset
    void SetSysExTemplate(std::vector<std::uint8_t> packet) {
        sysex_template_ = std::move(packet);
    }

    void Close() noexcept {
        if (midi_out_ != nullptr) {
            snd_rawmidi_close(midi_out_);
            midi_out_ = nullptr;
        }
        opened_device_.clear();
    }

private:
    bool EnsureOpen(const std::string& rawmidi_device) {
        if (rawmidi_device.empty()) {
            return false;
        }
        if (midi_out_ != nullptr && opened_device_ == rawmidi_device) {
            return true;
        }
        Close();
        if (snd_rawmidi_open(nullptr, &midi_out_, rawmidi_device.c_str(), SND_RAWMIDI_NONBLOCK) < 0) {
            midi_out_ = nullptr;
            return false;
        }
        opened_device_ = rawmidi_device;
        return true;
    }

    std::array<std::string, kNumKnobs> labels_{};
    std::array<std::atomic<std::uint8_t>, kNumKnobs> last_sent_val_{};
    std::array<std::atomic<std::uint8_t>, kNumKnobs> pending_val_{};
    std::array<std::atomic<bool>, kNumKnobs> dirty_{};
    std::array<std::chrono::steady_clock::time_point, kNumKnobs> last_send_tp_{};

    std::vector<std::uint8_t> sysex_template_;
    snd_rawmidi_t* midi_out_{nullptr};
    std::string opened_device_;
};

} // namespace flac_ax::midi
