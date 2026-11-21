// src/midi/mpk_mini3.hpp — Core 5 ALSA RawMIDI listener for Akai MPK Mini MK3
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

namespace flac_ax::midi {

enum class MidiMsgType : std::uint8_t {
    NoteOff = 0x80,
    NoteOn = 0x90,
    ControlChange = 0xB0,
    ProgramChange = 0xC0,
    ChannelPressure = 0xD0,
    PitchBend = 0xE0,
};

struct MidiEvent {
    MidiMsgType type{MidiMsgType::NoteOff};
    std::uint8_t channel{0};   // 0..15 (Ch 1 = keys/knobs, Ch 10 = pads)
    std::uint8_t data1{0};     // Note number or CC number (K1..K8 = CC 70..77)
    std::uint8_t data2{0};     // Velocity or CC value (0..127)
    std::int16_t pitch_bend{0}; // -8192..+8191
};

struct MpkMini3State {
    std::array<std::atomic<std::uint8_t>, 8> knobs{};      // CC 70..77 (0..127)
    std::array<std::atomic<std::uint8_t>, 16> pad_vel{};   // Pads 1..16 (Banks A & B)
    std::atomic<std::uint8_t> mod_wheel{0};                // CC 1
    std::atomic<std::uint8_t> sustain{0};                  // CC 64
    std::atomic<std::int16_t> pitch_bend{0};               // -8192..+8191
    std::atomic<bool> connected{false};
};

class MpkMini3Listener {
public:
    static constexpr std::size_t kQueueCapacity = 1024;
    static constexpr std::size_t kQueueMask = kQueueCapacity - 1;

    MpkMini3Listener();
    ~MpkMini3Listener();

    MpkMini3Listener(const MpkMini3Listener&) = delete;
    MpkMini3Listener& operator=(const MpkMini3Listener&) = delete;

    // Starts non-blocking ALSA RawMIDI polling pinned to Core 5 (housekeeping/USB HID domain)
    void Start(int core_id = 5, const std::string& explicit_device = "");
    void Stop();

    bool PopEvent(MidiEvent& out) noexcept;
    const MpkMini3State& state() const noexcept { return state_; }
    std::string active_device() const;

private:
    void RunLoop(int core_id, std::string explicit_device);
    void ParseByte(std::uint8_t byte) noexcept;
    void Enqueue(const MidiEvent& ev) noexcept;
    static std::string AutoDetectMpkDevice();

    MpkMini3State state_{};
    alignas(64) std::array<MidiEvent, kQueueCapacity> ring_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};

    std::atomic<bool> running_{false};
    std::thread worker_{};
    std::string detected_device_{};

    // Running status MIDI parser state
    std::uint8_t running_status_{0};
    std::array<std::uint8_t, 2> data_bytes_{};
    std::uint8_t data_index_{0};
    std::uint8_t expected_data_bytes_{0};
    bool in_sysex_{false};
};

} // namespace flac_ax::midi
