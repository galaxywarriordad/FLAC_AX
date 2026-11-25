#pragma once

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace flac_ax::midi {

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

inline std::string FormatKnobOledLabel(std::size_t knob_idx,
                                        std::string_view param_name,
                                        std::uint8_t value_0_127) {
    (void)knob_idx;
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

    void NotifyKnobValue(std::size_t knob_idx, std::uint8_t value_0_127) noexcept {
        if (knob_idx >= kNumKnobs) {
            return;
        }
        pending_val_[knob_idx].store(value_0_127 & 0x7FU, std::memory_order_relaxed);
        dirty_[knob_idx].store(true, std::memory_order_release);
    }

    void PollAndFlush(const std::string& rawmidi_device,
                      std::chrono::milliseconds min_interval = std::chrono::milliseconds(45)) {
#if defined(_WIN32)
        (void)rawmidi_device;
        (void)min_interval;
#else
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
                pending_val_[i].store(val, std::memory_order_relaxed);
                dirty_[i].store(true, std::memory_order_release);
                continue;
            }

            if (EnsureOpen(rawmidi_device)) {
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
#endif
    }

    void SetSysExTemplate(std::vector<std::uint8_t> packet) {
        sysex_template_ = std::move(packet);
    }

    void Close() noexcept {
#if !defined(_WIN32)
        if (midi_out_ != nullptr) {
            snd_rawmidi_close(midi_out_);
            midi_out_ = nullptr;
        }
#endif
        opened_device_.clear();
    }

private:
#if !defined(_WIN32)
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
#endif

    std::array<std::string, kNumKnobs> labels_{};
    std::array<std::atomic<std::uint8_t>, kNumKnobs> last_sent_val_{};
    std::array<std::atomic<std::uint8_t>, kNumKnobs> pending_val_{};
    std::array<std::atomic<bool>, kNumKnobs> dirty_{};
    std::array<std::chrono::steady_clock::time_point, kNumKnobs> last_send_tp_{};

    std::vector<std::uint8_t> sysex_template_;
#if !defined(_WIN32)
    snd_rawmidi_t* midi_out_{nullptr};
#endif
    std::string opened_device_;
};

} // namespace flac_ax::midi
