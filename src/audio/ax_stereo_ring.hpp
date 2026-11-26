#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flac_ax::audio {

template <typename SampleT>
class StereoSampleRing {
public:
    static constexpr std::size_t kDefaultCapacityFrames = 262144U;

    explicit StereoSampleRing(std::size_t capacity_frames = kDefaultCapacityFrames)
        : capacity_(NextPowerOfTwo(capacity_frames < 2U ? 2U : capacity_frames)),
          mask_(capacity_ - 1U),
          storage_(capacity_ * 2U, SampleT{}) {}

    [[nodiscard]] std::size_t capacity_frames() const noexcept { return capacity_; }

    // Touches backing storage before real-time threads start to avoid first-touch faults.
    void PrefaultStorage() noexcept {
        volatile SampleT sink{};
        const std::size_t stride = 4096U / sizeof(SampleT);
        const std::size_t step = stride > 0U ? stride : 1U;
        for (std::size_t i = 0U; i < storage_.size(); i += step) {
            sink = storage_[i];
        }
        if (!storage_.empty()) {
            sink = storage_.back();
        }
        (void)sink;
    }

    void Reset() noexcept {
        write_idx_.store(0ULL, std::memory_order_relaxed);
        read_idx_.store(0ULL, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t AvailableToRead() const noexcept {
        const std::uint64_t w = write_idx_.load(std::memory_order_acquire);
        const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
        return static_cast<std::size_t>(w - r);
    }

    [[nodiscard]] std::size_t AvailableToWrite() const noexcept {
        const std::uint64_t r = read_idx_.load(std::memory_order_acquire);
        const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
        const std::size_t used = static_cast<std::size_t>(w - r);
        return (used < capacity_) ? (capacity_ - used) : 0U;
    }

    [[nodiscard]] std::size_t PushInterleaved(const SampleT* interleaved_lr,
                                              std::size_t frames) noexcept {
        if (!interleaved_lr || frames == 0U) {
            return 0U;
        }
        const std::size_t can_write = std::min(frames, AvailableToWrite());
        const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
        for (std::size_t i = 0U; i < can_write; ++i) {
            const std::size_t slot = static_cast<std::size_t>((w + i) & mask_) * 2U;
            storage_[slot + 0U] = interleaved_lr[i * 2U + 0U];
            storage_[slot + 1U] = interleaved_lr[i * 2U + 1U];
        }
        write_idx_.store(w + can_write, std::memory_order_release);
        return can_write;
    }

    [[nodiscard]] std::size_t PopInterleaved(SampleT* out_lr,
                                             std::size_t max_frames) noexcept {
        if (!out_lr || max_frames == 0U) {
            return 0U;
        }
        const std::size_t can_read = std::min(max_frames, AvailableToRead());
        const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
        for (std::size_t i = 0U; i < can_read; ++i) {
            const std::size_t slot = static_cast<std::size_t>((r + i) & mask_) * 2U;
            out_lr[i * 2U + 0U] = storage_[slot + 0U];
            out_lr[i * 2U + 1U] = storage_[slot + 1U];
        }
        read_idx_.store(r + can_read, std::memory_order_release);
        return can_read;
    }

private:
    static constexpr std::size_t NextPowerOfTwo(std::size_t v) noexcept {
        std::size_t p = 1U;
        while (p < v) {
            p <<= 1U;
        }
        return p;
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    alignas(64) std::atomic<std::uint64_t> write_idx_{0ULL};
    alignas(64) std::atomic<std::uint64_t> read_idx_{0ULL};
    std::vector<SampleT> storage_;
};

using StereoFrameRing32 = StereoSampleRing<std::int32_t>;
using StereoFloatRing   = StereoSampleRing<float>;

[[nodiscard]] inline std::int32_t FloatSampleToS32(float v) noexcept {
    const float clamped = std::clamp(v, -1.0f, 1.0f);
    const double scaled = static_cast<double>(clamped) * 2147483647.0;
    return static_cast<std::int32_t>(std::llround(scaled));
}

inline void ConvertStereoFloatToS32(const float* in_lr,
                                   std::int32_t* out_lr,
                                   std::size_t frames) noexcept {
    if (!in_lr || !out_lr || frames == 0U) {
        return;
    }
    for (std::size_t i = 0U; i < frames * 2U; ++i) {
        out_lr[i] = FloatSampleToS32(in_lr[i]);
    }
}

} // namespace flac_ax::audio
