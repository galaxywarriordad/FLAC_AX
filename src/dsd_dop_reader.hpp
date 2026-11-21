// src/dsd_dop_reader.hpp — Zero-dependency C++20 .dsf and .dff header/stream reader for DoP
#pragma once

#include "ess_dac_hooks.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace flac_ax::dsd {

enum class DsdRateKind {
    Unsupported = 0,
    Dsd64,  // 2,822,400 Hz -> 176,400 Hz DoP
    Dsd128  // 5,644,800 Hz -> 352,800 Hz DoP
};

struct DsdStreamInfo {
    DsdRateKind rate_kind{DsdRateKind::Unsupported};
    unsigned int dsd_sample_rate_hz{0};
    unsigned int dop_pcm_rate_hz{0};
    unsigned int channels{0};
    std::uint64_t data_offset{0};
    std::uint64_t data_bytes{0};
    std::uint32_t dsf_block_size_per_channel{0};
    bool lsb_first{false};
    bool is_dff{false};
};

namespace detail {

inline std::uint16_t ReadBe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      static_cast<std::uint16_t>(p[1]));
}

inline std::uint32_t ReadBe32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

inline std::uint64_t ReadBe64(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint64_t>(ReadBe32(p)) << 32) |
           static_cast<std::uint64_t>(ReadBe32(p + 4));
}

inline std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t ReadLe64(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(ReadLe32(p)) |
           (static_cast<std::uint64_t>(ReadLe32(p + 4)) << 32);
}

inline bool FourCcEq(const std::uint8_t* p, const char (&tag)[5]) noexcept {
    return p[0] == static_cast<std::uint8_t>(tag[0]) &&
           p[1] == static_cast<std::uint8_t>(tag[1]) &&
           p[2] == static_cast<std::uint8_t>(tag[2]) &&
           p[3] == static_cast<std::uint8_t>(tag[3]);
}

inline std::uint8_t ReverseBits8(std::uint8_t b) noexcept {
    b = static_cast<std::uint8_t>(((b & 0xF0U) >> 4) | ((b & 0x0FU) << 4));
    b = static_cast<std::uint8_t>(((b & 0xCCU) >> 2) | ((b & 0x33U) << 2));
    b = static_cast<std::uint8_t>(((b & 0xAAU) >> 1) | ((b & 0x55U) << 1));
    return b;
}

inline DsdRateKind ClassifyDsdRate(unsigned int dsd_hz, unsigned int& out_dop_hz) noexcept {
    if (dsd_hz == 2822400U) {
        out_dop_hz = 176400U;
        return DsdRateKind::Dsd64;
    }
    if (dsd_hz == 5644800U) {
        out_dop_hz = 352800U;
        return DsdRateKind::Dsd128;
    }
    out_dop_hz = 0U;
    return DsdRateKind::Unsupported;
}

class ByteSpanReader {
public:
    ByteSpanReader(const std::uint8_t* data, std::uint64_t size) noexcept
        : data_(data), size_(size) {}

    std::uint64_t size() const noexcept { return size_; }

    bool ReadAt(std::uint64_t offset, std::uint8_t* dst, std::size_t count) const noexcept {
        if (offset > size_ || static_cast<std::uint64_t>(count) > (size_ - offset)) {
            return false;
        }
        if (count > 0) {
            std::memcpy(dst, data_ + offset, count);
        }
        return true;
    }

private:
    const std::uint8_t* data_{nullptr};
    std::uint64_t size_{0};
};

inline bool ParseDsfHeader(const ByteSpanReader& r, DsdStreamInfo& out) noexcept {
    // DSF header: "DSD " (4) + chunk_size LE64 (8) + total_file_size LE64 (8) + metadata_ptr LE64 (8) = 28 bytes
    if (r.size() < 28U + 12U) {
        return false;
    }
    std::array<std::uint8_t, 28> dsd_hdr{};
    if (!r.ReadAt(0, dsd_hdr.data(), dsd_hdr.size()) || !FourCcEq(dsd_hdr.data(), "DSD ")) {
        return false;
    }

    const std::uint64_t dsd_chunk_size = ReadLe64(dsd_hdr.data() + 4);
    if (dsd_chunk_size < 28U || dsd_chunk_size + 12U > r.size()) {
        return false;
    }

    // "fmt " chunk starts at dsd_chunk_size
    std::array<std::uint8_t, 12> fmt_hdr{};
    if (!r.ReadAt(dsd_chunk_size, fmt_hdr.data(), fmt_hdr.size()) || !FourCcEq(fmt_hdr.data(), "fmt ")) {
        return false;
    }
    const std::uint64_t fmt_size = ReadLe64(fmt_hdr.data() + 4);
    if (fmt_size < 52U || dsd_chunk_size + fmt_size > r.size()) {
        return false;
    }

    std::vector<std::uint8_t> fmt_body(static_cast<std::size_t>(fmt_size - 12U));
    if (!r.ReadAt(dsd_chunk_size + 12U, fmt_body.data(), fmt_body.size())) {
        return false;
    }

    // Offsets relative to start of fmt_body (byte 12 of fmt chunk):
    // format_version (4), format_id (4), channel_type (4), channel_num (4),
    // sampling_frequency (4), bits_per_sample (4), sample_count (8), block_size_per_channel (4)
    const unsigned int channels = ReadLe32(fmt_body.data() + 12);
    const unsigned int dsd_rate = ReadLe32(fmt_body.data() + 16);
    const unsigned int bps = ReadLe32(fmt_body.data() + 20);
    const std::uint32_t block_per_ch = ReadLe32(fmt_body.data() + 32);

    unsigned int dop_hz = 0U;
    const DsdRateKind kind = ClassifyDsdRate(dsd_rate, dop_hz);
    if (kind == DsdRateKind::Unsupported || channels < 2U || block_per_ch == 0U) {
        return false;
    }

    const std::uint64_t data_chunk_pos = dsd_chunk_size + fmt_size;
    if (data_chunk_pos + 12U > r.size()) {
        return false;
    }
    std::array<std::uint8_t, 12> data_hdr{};
    if (!r.ReadAt(data_chunk_pos, data_hdr.data(), data_hdr.size()) || !FourCcEq(data_hdr.data(), "data")) {
        return false;
    }
    const std::uint64_t data_chunk_size = ReadLe64(data_hdr.data() + 4);
    if (data_chunk_size <= 12U || data_chunk_pos + data_chunk_size > r.size()) {
        return false;
    }

    out = DsdStreamInfo{};
    out.rate_kind = kind;
    out.dsd_sample_rate_hz = dsd_rate;
    out.dop_pcm_rate_hz = dop_hz;
    out.channels = channels;
    out.data_offset = data_chunk_pos + 12U;
    out.data_bytes = data_chunk_size - 12U;
    out.dsf_block_size_per_channel = block_per_ch;
    out.lsb_first = (bps == 1U); // DSF standard is LSB-first per byte
    out.is_dff = false;
    return true;
}

inline bool ParsePropChunk(const ByteSpanReader& r,
                           std::uint64_t prop_payload_offset,
                           std::uint64_t prop_payload_size,
                           unsigned int& out_rate,
                           unsigned int& out_channels) noexcept {
    if (prop_payload_size < 4U) {
        return false;
    }
    std::array<std::uint8_t, 4> prop_type{};
    if (!r.ReadAt(prop_payload_offset, prop_type.data(), 4U) || !FourCcEq(prop_type.data(), "SND ")) {
        return false;
    }

    std::uint64_t cur = prop_payload_offset + 4U;
    const std::uint64_t end = prop_payload_offset + prop_payload_size;

    while (cur + 12U <= end) {
        std::array<std::uint8_t, 12> sub{};
        if (!r.ReadAt(cur, sub.data(), sub.size())) {
            return false;
        }
        const std::uint64_t sub_size = ReadBe64(sub.data() + 4);
        const std::uint64_t body_off = cur + 12U;
        if (body_off + sub_size > end) {
            return false;
        }

        if (FourCcEq(sub.data(), "FS  ") && sub_size >= 4U) {
            std::array<std::uint8_t, 4> fs_buf{};
            if (r.ReadAt(body_off, fs_buf.data(), 4U)) {
                out_rate = ReadBe32(fs_buf.data());
            }
        } else if (FourCcEq(sub.data(), "CHNL") && sub_size >= 2U) {
            std::array<std::uint8_t, 2> ch_buf{};
            if (r.ReadAt(body_off, ch_buf.data(), 2U)) {
                out_channels = ReadBe16(ch_buf.data());
            }
        }

        cur = body_off + sub_size + (sub_size & 1ULL);
    }

    return out_rate > 0U && out_channels >= 2U;
}

inline bool ParseDffHeader(const ByteSpanReader& r, DsdStreamInfo& out) noexcept {
    // DSDIFF: "FRM8" (4) + size BE64 (8) + "DSD " (4) = 16 bytes
    if (r.size() < 16U) {
        return false;
    }
    std::array<std::uint8_t, 16> frm{};
    if (!r.ReadAt(0, frm.data(), frm.size()) ||
        !FourCcEq(frm.data(), "FRM8") ||
        !FourCcEq(frm.data() + 12, "DSD ")) {
        return false;
    }

    const std::uint64_t form_end = std::min<std::uint64_t>(r.size(), 12U + ReadBe64(frm.data() + 4));
    std::uint64_t cur = 16U;
    unsigned int rate = 0U;
    unsigned int channels = 0U;
    bool found_dsd = false;
    std::uint64_t dsd_off = 0U;
    std::uint64_t dsd_len = 0U;

    while (cur + 12U <= form_end) {
        std::array<std::uint8_t, 12> ch_hdr{};
        if (!r.ReadAt(cur, ch_hdr.data(), ch_hdr.size())) {
            return false;
        }
        const std::uint64_t payload_size = ReadBe64(ch_hdr.data() + 4);
        const std::uint64_t payload_off = cur + 12U;
        if (payload_off + payload_size > form_end) {
            break;
        }

        if (FourCcEq(ch_hdr.data(), "PROP")) {
            (void)ParsePropChunk(r, payload_off, payload_size, rate, channels);
        } else if (FourCcEq(ch_hdr.data(), "DSD ")) {
            found_dsd = true;
            dsd_off = payload_off;
            dsd_len = payload_size;
        }

        cur = payload_off + payload_size + (payload_size & 1ULL);
    }

    unsigned int dop_hz = 0U;
    const DsdRateKind kind = ClassifyDsdRate(rate, dop_hz);
    if (!found_dsd || kind == DsdRateKind::Unsupported || channels < 2U || dsd_len < 4U) {
        return false;
    }

    out = DsdStreamInfo{};
    out.rate_kind = kind;
    out.dsd_sample_rate_hz = rate;
    out.dop_pcm_rate_hz = dop_hz;
    out.channels = channels;
    out.data_offset = dsd_off;
    out.data_bytes = dsd_len;
    out.dsf_block_size_per_channel = 0U;
    out.lsb_first = false; // DSDIFF is MSB-first per byte
    out.is_dff = true;
    return true;
}

} // namespace detail

// Parses .dsf or .dff headers from a memory buffer (e.g., RamFilePreloader::Entry::bytes)
inline bool ParseDsdMemoryHeader(const std::uint8_t* data,
                                 std::uint64_t size,
                                 DsdStreamInfo& out_info) noexcept {
    if (data == nullptr || size < 16U) {
        return false;
    }
    const detail::ByteSpanReader reader(data, size);
    if (detail::FourCcEq(data, "DSD ")) {
        return detail::ParseDsfHeader(reader, out_info);
    }
    if (detail::FourCcEq(data, "FRM8")) {
        return detail::ParseDffHeader(reader, out_info);
    }
    return false;
}

// Parses .dsf or .dff headers from a file on disk by reading up to the first 64 KiB
inline bool ParseDsdFileHeader(const std::string& path, DsdStreamInfo& out_info) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    std::array<std::uint8_t, 65536> head{};
    const std::size_t n = std::fread(head.data(), 1, head.size(), fp);
    std::fclose(fp);
    if (n < 16U) {
        return false;
    }
    return ParseDsdMemoryHeader(head.data(), static_cast<std::uint64_t>(n), out_info);
}

// Streams stereo S32_LE DoP frames from raw DSD data (DSF interleaved blocks or DFF stereo bytes)
class DsdDopStreamer {
public:
    void Configure(const DsdStreamInfo& info,
                   const std::uint8_t* mem_data,
                   std::uint64_t mem_size,
                   FILE* disk_file) noexcept {
        info_ = info;
        mem_data_ = mem_data;
        mem_size_ = mem_size;
        disk_file_ = disk_file;
        stream_pos_ = 0U;
        packer_.Reset();
    }

    // Produces up to `max_frames` stereo S32_LE DoP frames (each frame consumes 2 bytes/ch = 4 bytes stereo).
    std::size_t ReadDopFramesS32(std::int32_t* out_interleaved_lr, std::size_t max_frames) {
        if (out_interleaved_lr == nullptr || max_frames == 0U || stream_pos_ >= info_.data_bytes) {
            return 0U;
        }

        if (!info_.is_dff && info_.dsf_block_size_per_channel > 0U) {
            return ReadDsfBlockFrames(out_interleaved_lr, max_frames);
        }
        return ReadDffInterleavedFrames(out_interleaved_lr, max_frames);
    }

private:
    std::size_t ReadRawBytes(std::uint64_t rel_offset, std::uint8_t* dst, std::size_t count) {
        if (rel_offset >= info_.data_bytes || count == 0U) {
            return 0U;
        }
        const std::uint64_t rem_stream = info_.data_bytes - rel_offset;
        const std::size_t to_read = static_cast<std::size_t>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(count), rem_stream));

        if (mem_data_ != nullptr) {
            const std::uint64_t abs_off = info_.data_offset + rel_offset;
            if (abs_off >= mem_size_) {
                return 0U;
            }
            const std::size_t avail = static_cast<std::size_t>(
                std::min<std::uint64_t>(static_cast<std::uint64_t>(to_read), mem_size_ - abs_off));
            std::memcpy(dst, mem_data_ + abs_off, avail);
            return avail;
        }

        if (disk_file_ != nullptr) {
            const std::uint64_t abs_off = info_.data_offset + rel_offset;
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
            if (std::fseeko(disk_file_, static_cast<off_t>(abs_off), SEEK_SET) != 0) {
                return 0U;
            }
#else
            if (std::fseek(disk_file_, static_cast<long>(abs_off), SEEK_SET) != 0) {
                return 0U;
            }
#endif
            return std::fread(dst, 1, to_read, disk_file_);
        }

        return 0U;
    }

    std::uint8_t FixBitOrder(std::uint8_t b) const noexcept {
        return info_.lsb_first ? detail::ReverseBits8(b) : b;
    }

    std::size_t ReadDffInterleavedFrames(std::int32_t* out_lr, std::size_t max_frames) {
        const std::size_t bytes_per_frame = static_cast<std::size_t>(info_.channels) * 2U;
        if (bytes_per_frame < 4U) {
            return 0U;
        }
        const std::size_t want_frames = std::min<std::size_t>(max_frames, 1024U);
        std::vector<std::uint8_t> buf(want_frames * bytes_per_frame);
        const std::size_t got = ReadRawBytes(stream_pos_, buf.data(), buf.size());
        const std::size_t frames = got / bytes_per_frame;
        if (frames == 0U) {
            stream_pos_ = info_.data_bytes;
            return 0U;
        }

        for (std::size_t i = 0; i < frames; ++i) {
            const std::uint8_t* p = buf.data() + i * bytes_per_frame;
            const std::uint8_t l0 = FixBitOrder(p[0]);
            const std::uint8_t l1 = FixBitOrder(p[1]);
            const std::uint8_t r0 = FixBitOrder(p[2]);
            const std::uint8_t r1 = FixBitOrder(p[3]);

            const auto dsd_l = static_cast<std::uint16_t>((static_cast<std::uint16_t>(l0) << 8) | l1);
            const auto dsd_r = static_cast<std::uint16_t>((static_cast<std::uint16_t>(r0) << 8) | r1);
            packer_.PackStereoFrame(dsd_l, dsd_r, out_lr[i * 2 + 0], out_lr[i * 2 + 1]);
        }

        stream_pos_ += static_cast<std::uint64_t>(frames * bytes_per_frame);
        return frames;
    }

    std::size_t ReadDsfBlockFrames(std::int32_t* out_lr, std::size_t max_frames) {
        const std::size_t blk_per_ch = static_cast<std::size_t>(info_.dsf_block_size_per_channel);
        const std::size_t ch_count = static_cast<std::size_t>(info_.channels);
        const std::size_t total_block_bytes = blk_per_ch * ch_count;
        const std::size_t frames_per_block = blk_per_ch / 2U;
        if (frames_per_block == 0U || total_block_bytes == 0U) {
            stream_pos_ = info_.data_bytes;
            return 0U;
        }

        const std::uint64_t block_idx = stream_pos_ / static_cast<std::uint64_t>(total_block_bytes);
        const std::uint64_t block_rel_off = block_idx * static_cast<std::uint64_t>(total_block_bytes);
        const std::size_t in_block_frame =
            static_cast<std::size_t>((stream_pos_ - block_rel_off) / 2U);
        if (in_block_frame >= frames_per_block) {
            stream_pos_ = block_rel_off + static_cast<std::uint64_t>(total_block_bytes);
            return 0U;
        }

        std::vector<std::uint8_t> blk(total_block_bytes);
        const std::size_t got = ReadRawBytes(block_rel_off, blk.data(), blk.size());
        if (got < ch_count * 2U) {
            stream_pos_ = info_.data_bytes;
            return 0U;
        }

        const std::size_t avail_frames_in_blk =
            std::min(frames_per_block - in_block_frame, got / (ch_count * 2U));
        const std::size_t n_frames = std::min(max_frames, avail_frames_in_blk);

        const std::uint8_t* ch_l = blk.data();
        const std::uint8_t* ch_r = blk.data() + blk_per_ch;

        for (std::size_t i = 0; i < n_frames; ++i) {
            const std::size_t byte_i = (in_block_frame + i) * 2U;
            const std::uint8_t l0 = FixBitOrder(ch_l[byte_i + 0]);
            const std::uint8_t l1 = FixBitOrder(ch_l[byte_i + 1]);
            const std::uint8_t r0 = FixBitOrder(ch_r[byte_i + 0]);
            const std::uint8_t r1 = FixBitOrder(ch_r[byte_i + 1]);

            const auto dsd_l = static_cast<std::uint16_t>((static_cast<std::uint16_t>(l0) << 8) | l1);
            const auto dsd_r = static_cast<std::uint16_t>((static_cast<std::uint16_t>(r0) << 8) | r1);
            packer_.PackStereoFrame(dsd_l, dsd_r, out_lr[i * 2 + 0], out_lr[i * 2 + 1]);
        }

        stream_pos_ += static_cast<std::uint64_t>(n_frames * 2U);
        if (in_block_frame + n_frames >= frames_per_block) {
            stream_pos_ = block_rel_off + static_cast<std::uint64_t>(total_block_bytes);
        }
        return n_frames;
    }

    DsdStreamInfo info_{};
    const std::uint8_t* mem_data_{nullptr};
    std::uint64_t mem_size_{0};
    FILE* disk_file_{nullptr};
    std::uint64_t stream_pos_{0};
    flac_ax::ess::DopPacker packer_{};
};

} // namespace flac_ax::dsd
