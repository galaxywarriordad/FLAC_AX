// src/video/av_pts_sync.hpp — Audio-master PTS sync & capture drift tracker for FLAC_AX (720p)
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace ax::video {

enum class SyncMode : std::uint8_t {
    AudioMasterDvd = 0, // Video PTS paces against ESS Sabre ALSA audio stream
    LiveLowLatency      // Minimal delay for V4L2 retro console capture
};

enum class FramePacingAction : std::uint8_t {
    DropLate = 0, // Frame is already behind audio master clock
    PresentNow,   // Frame is due now or within early threshold
    HoldEarly     // Frame is ahead of audio master clock; wait until due
};

struct FramePacingDecision {
    FramePacingAction action{FramePacingAction::PresentNow};
    std::int64_t delta_us{0}; // (frame_pts_us - master_clock_us); negative = late, positive = early
    std::int64_t hold_us{0};  // Suggested sleep/wait duration when action == HoldEarly
};

struct SyncTelemetry {
    std::int64_t last_av_delta_us{0};
    double estimated_drift_ppm{0.0};
    std::uint64_t frames_presented{0ULL};
    std::uint64_t frames_dropped_late{0ULL};
    std::uint64_t frames_held_early{0ULL};
};

class AudioMasterPtsSync {
public:
    explicit AudioMasterPtsSync(std::uint32_t sample_rate_hz = 48000U,
                                SyncMode mode = SyncMode::AudioMasterDvd) noexcept
        : sample_rate_hz_(sample_rate_hz > 0U ? sample_rate_hz : 48000U),
          mode_(mode) {}

    void SetMode(SyncMode mode) noexcept {
        mode_.store(mode, std::memory_order_release);
    }

    SyncMode mode() const noexcept {
        return mode_.load(std::memory_order_acquire);
    }

    void SetSampleRate(std::uint32_t sample_rate_hz) noexcept {
        if (sample_rate_hz > 0U) {
            sample_rate_hz_.store(sample_rate_hz, std::memory_order_release);
        }
    }

    void Reset(std::int64_t initial_pts_us = 0) noexcept {
        audio_frames_written_.store(0ULL, std::memory_order_release);
        base_pts_us_.store(initial_pts_us, std::memory_order_release);
        last_delta_us_.store(0, std::memory_order_release);
        frames_presented_.store(0ULL, std::memory_order_release);
        frames_dropped_late_.store(0ULL, std::memory_order_release);
        frames_held_early_.store(0ULL, std::memory_order_release);
        has_prev_sample_.store(false, std::memory_order_release);
        drift_ppm_x1000_.store(0LL, std::memory_order_release);
    }

    // Called from real-time audio thread (zero allocations, zero locks) after frames are accepted.
    void OnAudioFramesCommitted(std::uint64_t frames,
                                std::chrono::steady_clock::time_point now =
                                    std::chrono::steady_clock::now()) noexcept {
        const std::uint64_t total_frames =
            audio_frames_written_.fetch_add(frames, std::memory_order_acq_rel) + frames;
        const std::uint32_t rate = sample_rate_hz_.load(std::memory_order_acquire);
        const std::int64_t audio_elapsed_us =
            static_cast<std::int64_t>((total_frames * 1000000ULL) / static_cast<std::uint64_t>(rate));
        const std::int64_t master_us = base_pts_us_.load(std::memory_order_acquire) + audio_elapsed_us;

        const std::int64_t wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                         now.time_since_epoch())
                                         .count();

        if (has_prev_sample_.load(std::memory_order_acquire)) {
            const std::int64_t prev_master = prev_master_us_.load(std::memory_order_acquire);
            const std::int64_t prev_wall = prev_wall_us_.load(std::memory_order_acquire);
            const std::int64_t d_master = master_us - prev_master;
            const std::int64_t d_wall = wall_us - prev_wall;
            if (d_wall > 50000 && d_master > 0) {
                const double ppm =
                    (static_cast<double>(d_master - d_wall) / static_cast<double>(d_wall)) * 1e6;
                const double clamped_ppm = std::clamp(ppm, -2000.0, 2000.0);
                drift_ppm_x1000_.store(static_cast<std::int64_t>(std::llround(clamped_ppm * 1000.0)),
                                       std::memory_order_release);
            }
        }

        prev_master_us_.store(master_us, std::memory_order_release);
        prev_wall_us_.store(wall_us, std::memory_order_release);
        has_prev_sample_.store(true, std::memory_order_release);
    }

    std::int64_t MasterClockUs() const noexcept {
        const std::uint64_t total_frames = audio_frames_written_.load(std::memory_order_acquire);
        const std::uint32_t rate = sample_rate_hz_.load(std::memory_order_acquire);
        const std::int64_t audio_elapsed_us =
            static_cast<std::int64_t>((total_frames * 1000000ULL) / static_cast<std::uint64_t>(rate));
        return base_pts_us_.load(std::memory_order_acquire) + audio_elapsed_us;
    }

    // Evaluates whether a video frame with timestamp `frame_pts_us` should be presented, held, or dropped.
    FramePacingDecision EvaluateVideoFrame(std::int64_t frame_pts_us,
                                           std::int64_t late_drop_threshold_us = -22000,
                                           std::int64_t early_hold_threshold_us = 4000,
                                           std::int64_t max_hold_us = 33333) noexcept {
        const std::int64_t now_us = MasterClockUs();
        const std::int64_t delta_us = frame_pts_us - now_us;
        last_delta_us_.store(delta_us, std::memory_order_release);

        FramePacingDecision out{};
        out.delta_us = delta_us;

        if (mode_.load(std::memory_order_acquire) == SyncMode::LiveLowLatency) {
            if (delta_us < (late_drop_threshold_us * 2)) {
                out.action = FramePacingAction::DropLate;
                frames_dropped_late_.fetch_add(1ULL, std::memory_order_relaxed);
                return out;
            }
            out.action = FramePacingAction::PresentNow;
            frames_presented_.fetch_add(1ULL, std::memory_order_relaxed);
            return out;
        }

        if (delta_us < late_drop_threshold_us) {
            out.action = FramePacingAction::DropLate;
            frames_dropped_late_.fetch_add(1ULL, std::memory_order_relaxed);
            return out;
        }

        if (delta_us > early_hold_threshold_us) {
            out.action = FramePacingAction::HoldEarly;
            out.hold_us = std::min<std::int64_t>(delta_us, max_hold_us);
            frames_held_early_.fetch_add(1ULL, std::memory_order_relaxed);
            return out;
        }

        out.action = FramePacingAction::PresentNow;
        frames_presented_.fetch_add(1ULL, std::memory_order_relaxed);
        return out;
    }

    SyncTelemetry SnapshotTelemetry() const noexcept {
        SyncTelemetry t{};
        t.last_av_delta_us = last_delta_us_.load(std::memory_order_acquire);
        t.estimated_drift_ppm =
            static_cast<double>(drift_ppm_x1000_.load(std::memory_order_acquire)) / 1000.0;
        t.frames_presented = frames_presented_.load(std::memory_order_acquire);
        t.frames_dropped_late = frames_dropped_late_.load(std::memory_order_acquire);
        t.frames_held_early = frames_held_early_.load(std::memory_order_acquire);
        return t;
    }

private:
    std::atomic<std::uint32_t> sample_rate_hz_{48000U};
    std::atomic<SyncMode> mode_{SyncMode::AudioMasterDvd};
    std::atomic<std::uint64_t> audio_frames_written_{0ULL};
    std::atomic<std::int64_t> base_pts_us_{0};
    std::atomic<std::int64_t> last_delta_us_{0};

    std::atomic<std::uint64_t> frames_presented_{0ULL};
    std::atomic<std::uint64_t> frames_dropped_late_{0ULL};
    std::atomic<std::uint64_t> frames_held_early_{0ULL};

    std::atomic<bool> has_prev_sample_{false};
    std::atomic<std::int64_t> prev_master_us_{0};
    std::atomic<std::int64_t> prev_wall_us_{0};
    std::atomic<std::int64_t> drift_ppm_x1000_{0LL};
};

} // namespace ax::video
