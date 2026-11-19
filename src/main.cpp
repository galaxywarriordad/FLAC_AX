// src/main.cpp — Bare-metal Linux FLAC -> ALSA (hw:CARD=FC3,DEV=0) prototype
#include <FLAC++/decoder.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK)
#if __has_include(<ax/audio.hpp>)
#include <ax/audio.hpp>
#define FLAC_AX_HAVE_AX_AUDIO_HPP 1
#endif
#if __has_include(<ax/topology.hpp>)
#include <ax/topology.hpp>
#define FLAC_AX_HAVE_AX_TOPOLOGY_HPP 1
#endif
#endif

namespace {

std::atomic<bool> g_stop_requested{false};

void HandleSignal(int /*sig*/) noexcept {
    g_stop_requested.store(true, std::memory_order_release);
}

// 64-byte cache-line-aligned SPSC stereo frame ring buffer (interleaved S32_LE L/R).
// Uses ax::audio::LockFreeStereoRing when available in ax-sdk, with a standalone
// fallback so standalone builds compile with zero external assumptions.
template <std::size_t CapacityFrames>
class StereoFrameRing {
public:
#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        return ax_ring_.PushBatch(interleaved_lr, frames);
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        return ax_ring_.PopBatch(out_interleaved_lr, max_frames);
    }

    std::size_t Size() const noexcept {
        return ax_ring_.Size();
    }
#else
    bool Push(const std::int32_t* interleaved_lr, std::size_t frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t used = (head + CapacityFrames - tail) % CapacityFrames;
        const std::size_t free_slots = (CapacityFrames - 1) - used;
        if (frames > free_slots) {
            return false;
        }

        const std::size_t first = std::min(frames, CapacityFrames - head);
        std::memcpy(&buffer_[head][0], interleaved_lr, first * 2 * sizeof(std::int32_t));
        const std::size_t second = frames - first;
        if (second > 0) {
            std::memcpy(&buffer_[0][0], interleaved_lr + first * 2, second * 2 * sizeof(std::int32_t));
        }

        head_.store((head + frames) % CapacityFrames, std::memory_order_release);
        return true;
    }

    std::size_t Pop(std::int32_t* out_interleaved_lr, std::size_t max_frames) noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t available = (head + CapacityFrames - tail) % CapacityFrames;
        const std::size_t to_pop = std::min(max_frames, available);
        if (to_pop == 0) {
            return 0;
        }

        const std::size_t first = std::min(to_pop, CapacityFrames - tail);
        std::memcpy(out_interleaved_lr, &buffer_[tail][0], first * 2 * sizeof(std::int32_t));
        const std::size_t second = to_pop - first;
        if (second > 0) {
            std::memcpy(out_interleaved_lr + first * 2, &buffer_[0][0], second * 2 * sizeof(std::int32_t));
        }

        tail_.store((tail + to_pop) % CapacityFrames, std::memory_order_release);
        return to_pop;
    }

    std::size_t Size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head + CapacityFrames - tail) % CapacityFrames;
    }
#endif

private:
#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    ax::audio::LockFreeStereoRing<CapacityFrames, std::int32_t> ax_ring_{};
#else
    alignas(64) std::array<std::array<std::int32_t, 2>, CapacityFrames> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
#endif
};

struct StreamMeta {
    std::atomic<unsigned int> sample_rate{0};
    std::atomic<unsigned int> bits_per_sample{0};
    std::atomic<unsigned int> channels{0};
    std::atomic<bool> meta_ready{false};
    std::atomic<bool> eof_or_error{false};
};

inline std::int32_t SampleToS32(FLAC__int32 sample, unsigned int bps) noexcept {
    if (bps >= 32U) {
        return static_cast<std::int32_t>(sample);
    }
    if (bps == 0U) {
        return 0;
    }
    const unsigned int shift = 32U - bps;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(sample) << shift);
}

class FlacStreamDecoder final : public FLAC::Decoder::File {
public:
    FlacStreamDecoder(StereoFrameRing<65536>& ring, StreamMeta& meta)
        : ring_(ring), meta_(meta) {}

protected:
    void on_metadata(const ::FLAC__StreamMetadata* metadata) override {
        if (metadata->type == ::FLAC__METADATA_TYPE_STREAMINFO) {
            meta_.sample_rate.store(metadata->data.stream_info.sample_rate, std::memory_order_release);
            meta_.bits_per_sample.store(metadata->data.stream_info.bits_per_sample, std::memory_order_release);
            meta_.channels.store(metadata->data.stream_info.channels, std::memory_order_release);
            meta_.meta_ready.store(true, std::memory_order_release);
        }
    }

    ::FLAC__StreamDecoderWriteStatus write_callback(
        const ::FLAC__Frame* frame,
        const FLAC__int32* const buffer[]) override {
        const std::size_t blocksize = frame->header.blocksize;
        const unsigned int bps = frame->header.bits_per_sample;
        const unsigned int channels = frame->header.channels;

        if (pcm_scratch_.size() < blocksize * 2) {
            pcm_scratch_.resize(blocksize * 2);
        }

        for (std::size_t i = 0; i < blocksize; ++i) {
            const std::int32_t l = SampleToS32(buffer[0][i], bps);
            const std::int32_t r = (channels >= 2U) ? SampleToS32(buffer[1][i], bps) : l;
            pcm_scratch_[i * 2 + 0] = l;
            pcm_scratch_[i * 2 + 1] = r;
        }

        std::size_t offset = 0;
        while (offset < blocksize && !g_stop_requested.load(std::memory_order_acquire)) {
            if (ring_.Push(pcm_scratch_.data() + offset * 2, blocksize - offset)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        return g_stop_requested.load(std::memory_order_acquire)
                   ? FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
                   : FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void error_callback(::FLAC__StreamDecoderErrorStatus status) override {
        std::cerr << "[FLAC] Decoder error: " << ::FLAC__StreamDecoderErrorStatusString[status] << "\n";
        meta_.eof_or_error.store(true, std::memory_order_release);
    }

private:
    StereoFrameRing<65536>& ring_;
    StreamMeta& meta_;
    std::vector<std::int32_t> pcm_scratch_;
};

void ApplyThreadAffinityAndPriority(int cpu_id, int sched_fifo_prio) noexcept {
    if (cpu_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(cpu_id), &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
            std::cerr << "[WARN] Failed to set CPU affinity to core " << cpu_id << "\n";
        }
    }

    if (sched_fifo_prio > 0) {
        sched_param param{};
        param.sched_priority = sched_fifo_prio;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            std::cerr << "[WARN] Failed to set SCHED_FIFO priority " << sched_fifo_prio
                      << " (check CAP_SYS_NICE / ulimit)\n";
        }
    }
}

bool ConfigureAlsaDevice(snd_pcm_t* pcm, unsigned int requested_rate, snd_pcm_uframes_t period_frames) {
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);

    int err = snd_pcm_hw_params_any(pcm, params);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_any failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_access failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S32_LE);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_format(S32_LE) failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_channels(pcm, params, 2);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_channels(2) failed: " << snd_strerror(err) << "\n";
        return false;
    }

    unsigned int rate = requested_rate;
    int dir = 0;
    err = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_rate_near failed: " << snd_strerror(err) << "\n";
        return false;
    }
    if (rate != requested_rate) {
        std::cout << "[ALSA] Requested " << requested_rate << " Hz, hardware settled at " << rate << " Hz\n";
    }

    snd_pcm_uframes_t period = period_frames;
    err = snd_pcm_hw_params_set_period_size_near(pcm, params, &period, &dir);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_period_size_near failed: " << snd_strerror(err) << "\n";
        return false;
    }

    snd_pcm_uframes_t buffer = period * 4;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params_set_buffer_size_near failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_hw_params failed: " << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_prepare(pcm);
    if (err < 0) {
        std::cerr << "[ALSA] snd_pcm_prepare failed: " << snd_strerror(err) << "\n";
        return false;
    }

    std::cout << "[ALSA] Configured rate=" << rate << " Hz, period=" << period
              << " frames, buffer=" << buffer << " frames (S32_LE stereo)\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: flac_ax <file.flac> [alsa_device=hw:CARD=FC3,DEV=0] [period_frames=96]\n";
        return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const std::string flac_path = argv[1];
    const std::string device = (argc >= 3) ? argv[2] : "hw:CARD=FC3,DEV=0";
    const snd_pcm_uframes_t period_frames =
        (argc >= 4) ? static_cast<snd_pcm_uframes_t>(std::stoul(argv[3])) : 96U;

#if defined(FLAC_AX_HAVE_AX_AUDIO_HPP)
    std::cout << "[AX] Using ax::audio::LockFreeStereoRing\n";
#else
    std::cout << "[AX] Using built-in 64B-aligned SPSC stereo ring fallback\n";
#endif

    StereoFrameRing<65536> ring;
    StreamMeta meta;
    std::atomic<bool> decode_finished{false};

    std::thread decoder_thread([&]() {
        // Pin non-real-time FLAC file I/O and frame decoding to Core 5 (0x20 quarantine)
        ApplyThreadAffinityAndPriority(5, 0);

        FlacStreamDecoder decoder(ring, meta);
        const FLAC__StreamDecoderInitStatus init_status = decoder.init(flac_path);
        if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            std::cerr << "[FLAC] Failed to initialize decoder for '" << flac_path << "' (status="
                      << FLAC__StreamDecoderInitStatusString[init_status] << ")\n";
            meta.eof_or_error.store(true, std::memory_order_release);
            decode_finished.store(true, std::memory_order_release);
            return;
        }

        decoder.process_until_end_of_stream();
        decoder.finish();
        decode_finished.store(true, std::memory_order_release);
    });

    while (!meta.meta_ready.load(std::memory_order_acquire) &&
           !meta.eof_or_error.load(std::memory_order_acquire) &&
           !g_stop_requested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (meta.eof_or_error.load(std::memory_order_acquire) ||
        g_stop_requested.load(std::memory_order_acquire)) {
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    const unsigned int sample_rate = meta.sample_rate.load(std::memory_order_acquire);
    const unsigned int bps = meta.bits_per_sample.load(std::memory_order_acquire);
    const unsigned int channels = meta.channels.load(std::memory_order_acquire);
    std::cout << "[FLAC] Stream: " << sample_rate << " Hz, " << bps << "-bit, " << channels << " ch\n";

    snd_pcm_t* pcm = nullptr;
    int open_err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (open_err < 0) {
        std::cerr << "[ALSA] Failed to open '" << device << "': " << snd_strerror(open_err) << "\n";
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    if (!ConfigureAlsaDevice(pcm, sample_rate, period_frames)) {
        snd_pcm_close(pcm);
        g_stop_requested.store(true, std::memory_order_release);
        decoder_thread.join();
        return 1;
    }

    // Pin real-time ALSA output thread to Core 0 (0x01) with SCHED_FIFO
    ApplyThreadAffinityAndPriority(0, 80);

    std::vector<std::int32_t> out_buffer(period_frames * 2, 0);
    std::uint64_t xruns = 0;
    std::uint64_t total_frames_written = 0;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        const std::size_t avail = ring.Size();
        if (avail == 0) {
            if (decode_finished.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
            continue;
        }

        const std::size_t to_write = std::min<std::size_t>(avail, period_frames);
        const std::size_t popped = ring.Pop(out_buffer.data(), to_write);
        if (popped == 0) {
            continue;
        }

        std::size_t frame_offset = 0;
        while (frame_offset < popped && !g_stop_requested.load(std::memory_order_acquire)) {
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, out_buffer.data() + frame_offset * 2, popped - frame_offset);
            if (written >= 0) {
                frame_offset += static_cast<std::size_t>(written);
                total_frames_written += static_cast<std::size_t>(written);
            } else if (written == -EAGAIN) {
                snd_pcm_wait(pcm, 10);
            } else if (written == -EPIPE) {
                ++xruns;
                const int rec_err = snd_pcm_recover(pcm, static_cast<int>(written), 1);
                if (rec_err < 0) {
                    std::cerr << "[ALSA] Failed to recover from EPIPE: " << snd_strerror(rec_err) << "\n";
                    g_stop_requested.store(true, std::memory_order_release);
                    break;
                }
            } else {
                std::cerr << "[ALSA] snd_pcm_writei error: " << snd_strerror(static_cast<int>(written)) << "\n";
                g_stop_requested.store(true, std::memory_order_release);
                break;
            }
        }
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);

    g_stop_requested.store(true, std::memory_order_release);
    decoder_thread.join();

    std::cout << "[DONE] Frames written=" << total_frames_written
              << ", rate=" << sample_rate << " Hz, xruns=" << xruns << "\n";
    return 0;
}
