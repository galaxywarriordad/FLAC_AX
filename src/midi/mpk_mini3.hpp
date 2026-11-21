// src/midi/mpk_mini3.cpp — Core 5 ALSA RawMIDI implementation for Akai MPK Mini MK3
#include "midi/mpk_mini3.hpp"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace flac_ax::midi {

MpkMini3Listener::MpkMini3Listener() {
    // Initialize K1..K8 to sensible midpoint defaults (64)
    for (auto& k : state_.knobs) {
        k.store(64, std::memory_order_relaxed);
    }
}

MpkMini3Listener::~MpkMini3Listener() {
    Stop();
}

void MpkMini3Listener::Start(int core_id, const std::string& explicit_device) {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    worker_ = std::thread(&MpkMini3Listener::RunLoop, this, core_id, explicit_device);
}

void MpkMini3Listener::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    state_.connected.store(false, std::memory_order_release);
}

bool MpkMini3Listener::PopEvent(MidiEvent& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (head == tail) {
        return false;
    }
    out = ring_[tail];
    tail_.store((tail + 1) & kQueueMask, std::memory_order_release);
    return true;
}

std::string MpkMini3Listener::active_device() const {
    return detected_device_;
}

void MpkMini3Listener::Enqueue(const MidiEvent& ev) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) & kQueueMask;
    if (next == tail_.load(std::memory_order_acquire)) {
        return; // Drop oldest overflow cleanly without blocking Core 5
    }
    ring_[head] = ev;
    head_.store(next, std::memory_order_release);
}

std::string MpkMini3Listener::AutoDetectMpkDevice() {
    std::ifstream cards("/proc/asound/cards");
    if (!cards.is_open()) {
        return "hw:1,0,0";
    }
    std::string line;
    int current_card = -1;
    while (std::getline(cards, line)) {
        int card_idx = -1;
        if (std::sscanf(line.c_str(), " %d [", &card_idx) == 1) {
            current_card = card_idx;
        }
        if (current_card >= 0 &&
            (line.find("MPKmini3") != std::string::npos ||
             line.find("MPK mini 3") != std::string::npos ||
             line.find("Akai") != std::string::npos)) {
            return "hw:" + std::to_string(current_card) + ",0,0";
        }
    }
    return "hw:CARD=MPKmini3,DEV=0";
}

void MpkMini3Listener::ParseByte(std::uint8_t byte) noexcept {
    if (byte >= 0xF8) {
        return; // Ignore MIDI real-time clock ticks (0xF8..0xFF)
    }
    if (byte == 0xF0) {
        in_sysex_ = true;
        return;
    }
    if (byte == 0xF7) {
        in_sysex_ = false;
        return;
    }
    if (in_sysex_) {
        return;
    }

    if ((byte & 0x80U) != 0) {
        running_status_ = byte;
        data_index_ = 0;
        const std::uint8_t cmd = byte & 0xF0U;
        if (cmd == 0xC0U || cmd == 0xD0U) {
            expected_data_bytes_ = 1;
        } else if (cmd >= 0x80U && cmd <= 0xE0U) {
            expected_data_bytes_ = 2;
        } else {
            expected_data_bytes_ = 0;
        }
        return;
    }

    if (expected_data_bytes_ == 0) {
        return;
    }

    data_bytes_[data_index_++] = byte & 0x7FU;
    if (data_index_ < expected_data_bytes_) {
        return;
    }
    data_index_ = 0;

    const std::uint8_t cmd = running_status_ & 0xF0U;
    const std::uint8_t ch = running_status_ & 0x0FU;

    MidiEvent ev{};
    ev.channel = ch;
    ev.data1 = data_bytes_[0];
    ev.data2 = (expected_data_bytes_ > 1) ? data_bytes_[1] : 0;

    switch (cmd) {
        case 0x90U:
            ev.type = (ev.data2 > 0) ? MidiMsgType::NoteOn : MidiMsgType::NoteOff;
            if (ch == 9U && ev.data1 >= 36U && ev.data1 < 52U) {
                state_.pad_vel[ev.data1 - 36U].store(
                    (ev.type == MidiMsgType::NoteOn) ? ev.data2 : 0, std::memory_order_relaxed);
            }
            Enqueue(ev);
            break;

        case 0x80U:
            ev.type = MidiMsgType::NoteOff;
            if (ch == 9U && ev.data1 >= 36U && ev.data1 < 52U) {
                state_.pad_vel[ev.data1 - 36U].store(0, std::memory_order_relaxed);
            }
            Enqueue(ev);
            break;

        case 0xB0U:
            ev.type = MidiMsgType::ControlChange;
            if (ev.data1 >= 70U && ev.data1 <= 77U) {
                state_.knobs[ev.data1 - 70U].store(ev.data2, std::memory_order_relaxed);
            } else if (ev.data1 == 1U) {
                state_.mod_wheel.store(ev.data2, std::memory_order_relaxed);
            } else if (ev.data1 == 64U) {
                state_.sustain.store(ev.data2, std::memory_order_relaxed);
            }
            Enqueue(ev);
            break;

        case 0xC0U:
            ev.type = MidiMsgType::ProgramChange;
            Enqueue(ev);
            break;

        case 0xD0U:
            ev.type = MidiMsgType::ChannelPressure;
            Enqueue(ev);
            break;

        case 0xE0U: {
            ev.type = MidiMsgType::PitchBend;
            const int raw14 = (static_cast<int>(ev.data2) << 7) | static_cast<int>(ev.data1);
            ev.pitch_bend = static_cast<std::int16_t>(raw14 - 8192);
            state_.pitch_bend.store(ev.pitch_bend, std::memory_order_relaxed);
            Enqueue(ev);
            break;
        }

        default:
            break;
    }
}

void MpkMini3Listener::RunLoop(int core_id, std::string explicit_device) {
    if (core_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(core_id), &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }

    snd_rawmidi_t* midi_in = nullptr;
    std::array<std::uint8_t, 128> rx_buf{};

    while (running_.load(std::memory_order_acquire)) {
        if (midi_in == nullptr) {
            const std::string dev = explicit_device.empty() ? AutoDetectMpkDevice() : explicit_device;
            if (snd_rawmidi_open(&midi_in, nullptr, dev.c_str(), SND_RAWMIDI_NONBLOCK) == 0) {
                detected_device_ = dev;
                state_.connected.store(true, std::memory_order_release);
            } else {
                state_.connected.store(false, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        const ssize_t n = snd_rawmidi_read(midi_in, rx_buf.data(), rx_buf.size());
        if (n > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                ParseByte(rx_buf[static_cast<std::size_t>(i)]);
            }
        } else if (n == -EAGAIN) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        } else {
            // Hot-unplug recovery
            snd_rawmidi_close(midi_in);
            midi_in = nullptr;
            state_.connected.store(false, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    if (midi_in != nullptr) {
        snd_rawmidi_close(midi_in);
    }
}

} // namespace flac_ax::midi
