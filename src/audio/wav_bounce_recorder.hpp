// src/audio/wav_bounce_recorder.hpp — Lock-free RT-safe 24/32-bit PCM WAV bounce recorder for Core 0 -> Core 5
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace flac_ax::audio {

class WavBounceRecorder {
public:
    static constexpr std::size_t kRingFrames = 262144U; // ~5.46s @ 48 kHz
    static constexpr std::size_t kRingMask   = kRingFrames - 1U;

    WavBounceRecorder() = default;
    ~WavBounceRecorder() { Close(); }

    WavBounceRecorder(const WavBounceRecorder&) = delete;
    WavBounceRecorder& operator=(const WavBounceRecorder&) = delete;

    bool Open(const std::string& path, unsigned int sample_rate_hz, unsigned int bit_depth = 24U) {
        Close();
        if (path.empty() || sample_rate_hz == 0U) {
            return false;
        }
        bits_per_sample_ = (bit_depth == 32U) ? 32U : 24U;
        sample_rate_ = sample_rate_hz;
        ring_.assign(kRingFrames * 2U, 0);
        write_idx_.store(0ULL, std::memory_order_relaxed);
        read_idx_.store(0ULL, std::memory_order_relaxed);
        overrun_frames_.store(0ULL, std::memory_order_relaxed);
        frames_written_ = 0ULL;

        file_ = std::fopen(path.c_str(), "wb");
        if (!file_) {
            return false;
        }
        // Write placeholder 44-byte RIFF/WAVE header (patched with exact sizes on Close())
        std::array<std::uint8_t, 44> blank{};
        if (std::fwrite(blank.data(), 1U, blank.size(), file_) != blank.size()) {
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        active_.store(true, std::memory_order_release);
        return true;
    }

    // Strictly wait-free SPSC producer called from Core 0 SCHED_FIFO 80 audio loop
    void PushFromRtThread(const std::int32_t* interleaved_lr_s32, std::size_t frames) noexcept {
        if (!active_.load(std::memory_order_acquire) || !interleaved_lr_s32 || frames == 0U) {
            return;
        }
        const std::uint64_t r = read_idx_.load(std::memory_order_acquire);
        const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
        const std::size_t used = static_cast<std::size_t>(w - r);
        const std::size_t avail = (used < kRingFrames) ? (kRingFrames - used) : 0U;
        const std::size_t to_copy = std::min(frames, avail);

        if (to_copy < frames) {
            overrun_frames_.fetch_add(static_cast<std::uint64_t>(frames - to_copy), std::memory_order_relaxed);
        }

        for (std::size_t i = 0U; i < to_copy; ++i) {
            const std::size_t slot = static_cast<std::size_t>((w + i) & kRingMask) * 2U;
            ring_[slot + 0U] = interleaved_lr_s32[i * 2U + 0U];
            ring_[slot + 1U] = interleaved_lr_s32[i * 2U + 1U];
        }
        write_idx_.store(w + to_copy, std::memory_order_release);
    }

    // Off-RT consumer called from Core 5 housekeeping / UI thread
    std::size_t DrainToDisk() {
        if (!file_ || !active_.load(std::memory_order_acquire)) {
            return 0U;
        }
        const std::uint64_t w = write_idx_.load(std::memory_order_acquire);
        const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
        const std::size_t avail = static_cast<std::size_t>(w - r);
        if (avail == 0U) {
            return 0U;
        }

        const std::size_t bytes_per_sample = bits_per_sample_ / 8U;
        const std::size_t bytes_per_frame = bytes_per_sample * 2U;
        byte_scratch_.resize(avail * bytes_per_frame);

        std::size_t out_offset = 0U;
        for (std::size_t i = 0U; i < avail; ++i) {
            const std::size_t slot = static_cast<std::size_t>((r + i) & kRingMask) * 2U;
            for (std::size_t ch = 0U; ch < 2U; ++ch) {
                const auto u = static_cast<std::uint32_t>(ring_[slot + ch]);
                if (bits_per_sample_ == 24U) {
                    // Top 24 bits of S32_LE in little-endian PCM
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 8) & 0xFFU);
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 16) & 0xFFU);
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 24) & 0xFFU);
                } else {
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>(u & 0xFFU);
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 8) & 0xFFU);
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 16) & 0xFFU);
                    byte_scratch_[out_offset++] = static_cast<std::uint8_t>((u >> 24) & 0xFFU);
                }
            }
        }

        const std::size_t written_bytes = std::fwrite(byte_scratch_.data(), 1U, out_offset, file_);
        const std::size_t written_frames = written_bytes / bytes_per_frame;
        frames_written_ += static_cast<std::uint64_t>(written_frames);
        read_idx_.store(r + avail, std::memory_order_release);
        return written_frames;
    }

    void Close() noexcept {
        if (file_) {
            (void)DrainToDisk();
            active_.store(false, std::memory_order_release);

            const std::uint16_t channels = 2U;
            const std::uint16_t block_align = static_cast<std::uint16_t>(channels * (bits_per_sample_ / 8U));
            const std::uint32_t byte_rate = sample_rate_ * block_align;
            const std::uint64_t raw_data_bytes = frames_written_ * block_align;
            const auto data_chunk_size = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(raw_data_bytes, 0xFFFFFFFFULL - 36ULL));
            const std::uint32_t riff_chunk_size = 36U + data_chunk_size;

            std::array<std::uint8_t, 44> hdr{};
            std::memcpy(hdr.data() + 0, "RIFF", 4);
            WriteU32Le(hdr.data() + 4, riff_chunk_size);
            std::memcpy(hdr.data() + 8, "WAVE", 4);
            std::memcpy(hdr.data() + 12, "fmt ", 4);
            WriteU32Le(hdr.data() + 16, 16U); // PCM fmt chunk
            WriteU16Le(hdr.data() + 20, 1U);  // WAVE_FORMAT_PCM
            WriteU16Le(hdr.data() + 22, channels);
            WriteU32Le(hdr.data() + 24, sample_rate_);
            WriteU32Le(hdr.data() + 28, byte_rate);
            WriteU16Le(hdr.data() + 32, block_align);
            WriteU16Le(hdr.data() + 34, static_cast<std::uint16_t>(bits_per_sample_));
            std::memcpy(hdr.data() + 36, "data", 4);
            WriteU32Le(hdr.data() + 40, data_chunk_size);

            std::fseek(file_, 0L, SEEK_SET);
            (void)std::fwrite(hdr.data(), 1U, hdr.size(), file_);
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }
        active_.store(false, std::memory_order_release);
    }

    bool is_active() const noexcept { return active_.load(std::memory_order_acquire); }
    std::uint64_t frames_written() const noexcept { return frames_written_; }
    std::uint64_t overrun_frames() const noexcept { return overrun_frames_.load(std::memory_order_relaxed); }

private:
    static void WriteU16Le(std::uint8_t* p, std::uint16_t v) noexcept {
        p[0] = static_cast<std::uint8_t>(v & 0xFFU);
        p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    }
    static void WriteU32Le(std::uint8_t* p, std::uint32_t v) noexcept {
        p[0] = static_cast<std::uint8_t>(v & 0xFFU);
        p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
        p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFU);
        p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFU);
    }

    std::FILE* file_{nullptr};
    unsigned int sample_rate_{48000U};
    unsigned int bits_per_sample_{24U};
    std::uint64_t frames_written_{0ULL};

    std::atomic<bool> active_{false};
    alignas(64) std::atomic<std::uint64_t> write_idx_{0ULL};
    alignas(64) std::atomic<std::uint64_t> read_idx_{0ULL};
    std::atomic<std::uint64_t> overrun_frames_{0ULL};

    std::vector<std::int32_t> ring_;
    std::vector<std::uint8_t> byte_scratch_;
};

} // namespace flac_ax::audio
