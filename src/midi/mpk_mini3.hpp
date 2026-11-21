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
    None = 0,
    NoteOn,
    NoteOff,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend
};

struct MidiEvent {
    MidiMsgType type{MidiMsgType::None};
    std::uint8_t channel{0};
    std::uint8_t data1{0};
    std::uint8_t data2{0};
    std::int16_t pitch_bend{0};
};

struct MpkMini3State {
    std::atomic<bool> connected{false};
    std::array<std::atomic<std::uint8_t>, 16> pad_vel{};
    std::array<std::atomic<std::uint8_t>, 8> knobs{};
    std::atomic<std::uint8_t> mod_wheel{0};
    std::atomic<std::uint8_t> sustain{0};
    std::atomic<std::int16_t> pitch_bend{0};
};

class MpkMini3Listener {
public:
    static constexpr std::size_t kQueueCapacity = 1024; // Must remain a power of 2
    static constexpr std::size_t kQueueMask = kQueueCapacity - 1;

    MpkMini3Listener();
    ~MpkMini3Listener();

    MpkMini3Listener(const MpkMini3Listener&) = delete;
    MpkMini3Listener& operator=(const MpkMini3Listener&) = delete;

    void Start(int core_id = 5, const std::string& explicit_device = {});
    void Stop();

    bool PopEvent(MidiEvent& out) noexcept;
    const MpkMini3State& state() const noexcept { return state_; }
    std::string active_device() const;

private:
    static std::string AutoDetectMpkDevice();

    void Enqueue(const MidiEvent& ev) noexcept;
    void ParseByte(std::uint8_t byte) noexcept;
    void RunLoop(int core_id, std::string explicit_device);

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string detected_device_;
    MpkMini3State state_{};

    alignas(64) std::array<MidiEvent, kQueueCapacity> ring_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    char pad_head_[64 - sizeof(std::atomic<std::size_t>)]{};
    alignas(64) std::atomic<std::size_t> tail_{0};
    char pad_tail_[64 - sizeof(std::atomic<std::size_t>)]{};

    std::uint8_t running_status_{0};
    std::array<std::uint8_t, 2> data_bytes_{};
    std::size_t data_index_{0};
    std::size_t expected_data_bytes_{0};
    bool in_sysex_{false};
};

} // namespace flac_ax::midi
