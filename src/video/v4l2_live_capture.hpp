// src/video/v4l2_live_capture.hpp — Low-latency V4L2 MMAP (YUYV / MJPEG) capture source for Core 5
#pragma once

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace ax::video {

enum class CapturePixelFormat : std::uint8_t {
    Unknown = 0,
    Yuyv422,
    Mjpeg
};

struct CapturedFrame {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::uint32_t bytes_per_line{0U};
    CapturePixelFormat format{CapturePixelFormat::Unknown};
    std::uint64_t sequence{0ULL};
    std::chrono::steady_clock::time_point monotonic_ts{};
    std::vector<std::uint8_t> payload;
};

class V4l2LiveCaptureSource {
public:
    static constexpr std::uint32_t kDefaultBufferCount = 3U;

    V4l2LiveCaptureSource() = default;
    ~V4l2LiveCaptureSource() { Stop(); }

    V4l2LiveCaptureSource(const V4l2LiveCaptureSource&) = delete;
    V4l2LiveCaptureSource& operator=(const V4l2LiveCaptureSource&) = delete;

    // Starts the capture loop pinned to `pin_core` (default Core 5 / storage_aux mask 0x20).
    // Requests `width` x `height` at `fps`, preferring YUYV and falling back to MJPEG.
    bool Start(const std::string& device_path,
               std::uint32_t width = 720U,
               std::uint32_t height = 480U,
               std::uint32_t fps = 60U,
               int pin_core = 5) {
        Stop();

        fd_ = ::open(device_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            return false;
        }

        if (!ConfigureFormatAndBuffers(width, height, fps, kDefaultBufferCount)) {
            CloseDevice();
            return false;
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            CloseDevice();
            return false;
        }

        for (auto& slot : slots_) {
            slot.seq.store(0ULL, std::memory_order_relaxed);
            slot.frame = CapturedFrame{};
        }
        active_slot_.store(0U, std::memory_order_relaxed);
        last_consumed_seq_.store(0ULL, std::memory_order_relaxed);
        dropped_frames_.store(0ULL, std::memory_order_relaxed);

        streaming_.store(true, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&V4l2LiveCaptureSource::CaptureLoop, this, pin_core);
        return true;
    }

    void Stop() noexcept {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (streaming_.exchange(false, std::memory_order_acq_rel) && fd_ >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            (void)::ioctl(fd_, VIDIOC_STREAMOFF, &type);
        }
        UnmapBuffers();
        CloseDevice();
    }

    // Lock-free seqlock copy of the latest published frame for Core 1 display/upscaling.
    // Returns false if no new frame is available or if a concurrent write invalidated the read.
    bool CopyLatestFrame(CapturedFrame& dst, std::uint64_t& last_seen_seq) const {
        const std::uint32_t idx = active_slot_.load(std::memory_order_acquire);
        const FrameSlot& slot = slots_[idx];

        const std::uint64_t seq1 = slot.seq.load(std::memory_order_acquire);
        if ((seq1 & 1ULL) != 0ULL || seq1 == 0ULL || seq1 == last_seen_seq) {
            return false;
        }

        const CapturedFrame& src = slot.frame;
        if (src.payload.empty()) {
            return false;
        }

        dst.width = src.width;
        dst.height = src.height;
        dst.bytes_per_line = src.bytes_per_line;
        dst.format = src.format;
        dst.sequence = src.sequence;
        dst.monotonic_ts = src.monotonic_ts;
        dst.payload.assign(src.payload.begin(), src.payload.end());

        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint64_t seq2 = slot.seq.load(std::memory_order_relaxed);
        if (seq1 != seq2) {
            return false; // Writer touched this slot mid-copy; retry on next poll
        }

        last_seen_seq = seq1;
        last_consumed_seq_.store(seq1, std::memory_order_relaxed);
        return true;
    }

    CapturePixelFormat active_format() const noexcept { return negotiated_format_; }
    std::uint32_t width() const noexcept { return negotiated_width_; }
    std::uint32_t height() const noexcept { return negotiated_height_; }
    std::uint64_t dropped_frames() const noexcept { return dropped_frames_.load(std::memory_order_relaxed); }

private:
    struct MmapBuffer {
        void* start{MAP_FAILED};
        std::size_t length{0U};
    };

    struct FrameSlot {
        alignas(64) std::atomic<std::uint64_t> seq{0ULL}; // Even = stable, Odd = writer active
        CapturedFrame frame{};
    };

    static int Xioctl(int fd, unsigned long request, void* arg) noexcept {
        int r = 0;
        do {
            r = ::ioctl(fd, request, arg);
        } while (r < 0 && errno == EINTR);
        return r;
    }

    bool TrySetPixelFormat(std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t fourcc,
                            v4l2_format& out_fmt) noexcept {
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = fourcc;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;

        if (Xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            return false;
        }
        if (fmt.fmt.pix.pixelformat != fourcc) {
            return false;
        }
        out_fmt = fmt;
        return true;
    }

    bool ConfigureFormatAndBuffers(std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t fps,
                                   std::uint32_t req_count) {
        v4l2_capability cap{};
        if (Xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
            return false;
        }
        const bool has_streaming = (cap.capabilities & V4L2_CAP_STREAMING) != 0U;
        const bool has_capture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0U;
        if (!has_streaming || !has_capture) {
            return false;
        }

        v4l2_format chosen{};
        if (TrySetPixelFormat(width, height, V4L2_PIX_FMT_YUYV, chosen)) {
            negotiated_format_ = CapturePixelFormat::Yuyv422;
        } else if (TrySetPixelFormat(width, height, V4L2_PIX_FMT_MJPEG, chosen)) {
            negotiated_format_ = CapturePixelFormat::Mjpeg;
        } else {
            return false;
        }

        negotiated_width_ = chosen.fmt.pix.width;
        negotiated_height_ = chosen.fmt.pix.height;
        negotiated_stride_ = chosen.fmt.pix.bytesperline;

        if (fps > 0U) {
            v4l2_streamparm parm{};
            parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            parm.parm.capture.timeperframe.numerator = 1U;
            parm.parm.capture.timeperframe.denominator = fps;
            (void)Xioctl(fd_, VIDIOC_S_PARM, &parm);
        }

        v4l2_requestbuffers req{};
        req.count = req_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (Xioctl(fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2U) {
            return false;
        }

        buffers_.resize(req.count);
        for (std::uint32_t i = 0U; i < req.count; ++i) {
            v4l2_buffer qbuf{};
            qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            qbuf.memory = V4L2_MEMORY_MMAP;
            qbuf.index = i;
            if (Xioctl(fd_, VIDIOC_QUERYBUF, &qbuf) < 0) {
                UnmapBuffers();
                return false;
            }
            buffers_[i].length = qbuf.length;
            buffers_[i].start = ::mmap(nullptr, qbuf.length,
                                        PROT_READ | PROT_WRITE, MAP_SHARED,
                                        fd_, qbuf.m.offset);
            if (buffers_[i].start == MAP_FAILED) {
                UnmapBuffers();
                return false;
            }
            if (Xioctl(fd_, VIDIOC_QBUF, &qbuf) < 0) {
                UnmapBuffers();
                return false;
            }
        }
        return true;
    }

    void UnmapBuffers() noexcept {
        for (auto& b : buffers_) {
            if (b.start != MAP_FAILED && b.start != nullptr && b.length > 0U) {
                ::munmap(b.start, b.length);
            }
            b.start = MAP_FAILED;
            b.length = 0U;
        }
        buffers_.clear();
    }

    void CloseDevice() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    static void PinCaptureThread(int core_id) noexcept {
        if (core_id >= 0) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(static_cast<unsigned int>(core_id), &set);
            (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
        }
    }

    void CaptureLoop(int pin_core) {
        PinCaptureThread(pin_core);
        std::uint64_t publish_counter = 0ULL;

        while (running_.load(std::memory_order_acquire)) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd_, &rfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 20000; // 20 ms poll timeout so shutdown responds promptly

            const int ready = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ready == 0) {
                continue;
            }

            v4l2_buffer dq{};
            dq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            dq.memory = V4L2_MEMORY_MMAP;
            if (Xioctl(fd_, VIDIOC_DQBUF, &dq) < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                break;
            }

            if (dq.index < buffers_.size()) {
                const auto* bytes = static_cast<const std::uint8_t*>(buffers_[dq.index].start);
                const std::size_t used = std::min<std::size_t>(
                    static_cast<std::size_t>(dq.bytesused), buffers_[dq.index].length);

                const std::uint32_t back = 1U - active_slot_.load(std::memory_order_relaxed);
                FrameSlot& slot = slots_[back];

                // Begin seqlock write (odd sequence number)
                const std::uint64_t odd_seq = (publish_counter * 2ULL) + 1ULL;
                const std::uint64_t even_seq = odd_seq + 1ULL;
                slot.seq.store(odd_seq, std::memory_order_release);

                slot.frame.width = negotiated_width_;
                slot.frame.height = negotiated_height_;
                slot.frame.bytes_per_line = negotiated_stride_;
                slot.frame.format = negotiated_format_;
                slot.frame.sequence = dq.sequence;
                slot.frame.monotonic_ts = std::chrono::steady_clock::now();
                slot.frame.payload.assign(bytes, bytes + used);

                // Commit seqlock write (even sequence number) and publish slot index
                slot.seq.store(even_seq, std::memory_order_release);
                active_slot_.store(back, std::memory_order_release);
                ++publish_counter;

                const std::uint64_t last_read = last_consumed_seq_.load(std::memory_order_relaxed);
                if (last_read > 0ULL && (even_seq - last_read) > 2ULL) {
                    dropped_frames_.fetch_add(1ULL, std::memory_order_relaxed);
                }
            }

            if (Xioctl(fd_, VIDIOC_QBUF, &dq) < 0) {
                break;
            }
        }
    }

    int fd_{-1};
    std::atomic<bool> running_{false};
    std::atomic<bool> streaming_{false};
    std::thread worker_;

    CapturePixelFormat negotiated_format_{CapturePixelFormat::Unknown};
    std::uint32_t negotiated_width_{0U};
    std::uint32_t negotiated_height_{0U};
    std::uint32_t negotiated_stride_{0U};

    std::vector<MmapBuffer> buffers_;
    std::array<FrameSlot, 2> slots_{};
    std::atomic<std::uint32_t> active_slot_{0U};
    mutable std::atomic<std::uint64_t> last_consumed_seq_{0ULL};
    std::atomic<std::uint64_t> dropped_frames_{0ULL};
};

} // namespace ax::video
