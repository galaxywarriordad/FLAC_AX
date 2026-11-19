// src/preload_policy.hpp — Host RAM detection and preload window policy for FLAC_AX
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace flac_ax {

enum class PreloadMode {
    SingleFile,   // < ~8 GB tier fallback
    Window3Files, // ~8 GB RAM: preload up to 3 files at a time
    WholeAlbum    // >= 16 GB RAM: preload whole album into RAM
};

struct PreloadPolicy {
    PreloadMode mode{PreloadMode::SingleFile};
    std::size_t max_window_files{1};
    std::size_t max_total_bytes{0}; // 0 = no explicit cap beyond window/RAM guard
    std::uint64_t detected_mem_total_bytes{0};
    std::uint64_t detected_mem_available_bytes{0};
};

// Parses /proc/meminfo for MemTotal and MemAvailable (in KiB).
inline void ReadLinuxMemInfo(std::uint64_t& mem_total_bytes,
                             std::uint64_t& mem_available_bytes) noexcept {
    mem_total_bytes = 0;
    mem_available_bytes = 0;

    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return;
    }

    std::string key;
    std::uint64_t kb = 0;
    std::string unit;
    while (meminfo >> key >> kb) {
        if (key == "MemTotal:") {
            mem_total_bytes = kb * 1024ULL;
        } else if (key == "MemAvailable:") {
            mem_available_bytes = kb * 1024ULL;
        }
        // Consume rest of line if unit is present
        std::getline(meminfo, unit);
        if (mem_total_bytes > 0 && mem_available_bytes > 0) {
            break;
        }
    }
}

// Evaluates preload policy from total (and optionally available) RAM in bytes.
// - >= 16 GiB total RAM: preload whole album
// - >= 6.5 GiB and < 16 GiB total RAM (8 GB class): preload 3 files at a time
// - < 6.5 GiB: single file
inline PreloadPolicy EvaluatePreloadPolicy(
    std::uint64_t mem_total_bytes,
    std::uint64_t mem_available_bytes = 0) noexcept {
    constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kWholeAlbumThreshold = 16ULL * kGiB;
    // Allow slight headroom below 8 GiB (e.g., reserved firmware/kernel carveouts)
    constexpr std::uint64_t kEightGbTierMin = (6ULL * kGiB) + (512ULL * 1024ULL * 1024ULL);

    PreloadPolicy policy{};
    policy.detected_mem_total_bytes = mem_total_bytes;
    policy.detected_mem_available_bytes = mem_available_bytes;

    if (mem_total_bytes >= kWholeAlbumThreshold) {
        policy.mode = PreloadMode::WholeAlbum;
        policy.max_window_files = std::numeric_limits<std::size_t>::max();
        // Optional safety cap (e.g., 8 GiB) so even huge albums leave plenty of RAM
        policy.max_total_bytes = static_cast<std::size_t>(8ULL * kGiB);
    } else if (mem_total_bytes >= kEightGbTierMin) {
        policy.mode = PreloadMode::Window3Files;
        policy.max_window_files = 3;
        // Guard against tight free RAM if MemAvailable is known and low
        if (mem_available_bytes > 0 && mem_available_bytes < (2ULL * kGiB)) {
            policy.max_window_files = 1;
            policy.mode = PreloadMode::SingleFile;
        }
        policy.max_total_bytes = static_cast<std::size_t>(2ULL * kGiB);
    } else {
        policy.mode = PreloadMode::SingleFile;
        policy.max_window_files = 1;
        policy.max_total_bytes = static_cast<std::size_t>(512ULL * 1024ULL * 1024ULL);
    }

    return policy;
}

inline PreloadPolicy DetectHostPreloadPolicy() noexcept {
    std::uint64_t total = 0;
    std::uint64_t avail = 0;
    ReadLinuxMemInfo(total, avail);
    return EvaluatePreloadPolicy(total, avail);
}

// Manages an in-RAM sliding/album preload cache of encoded FLAC files.
class RamFilePreloader {
public:
    struct Entry {
        std::string path;
        std::vector<std::uint8_t> bytes;
    };

    explicit RamFilePreloader(PreloadPolicy policy)
        : policy_(policy) {}

    const PreloadPolicy& policy() const noexcept { return policy_; }

    // Ensures queue[current_index .. end) is loaded into RAM according to policy.
    // For Window3Files: keeps up to 3 files starting from current_index, dropping older ones.
    // For WholeAlbum: loads all remaining files from current_index to end (within max_total_bytes).
    void RefreshWindow(const std::vector<std::string>& album_files, std::size_t current_index) {
        if (current_index >= album_files.size()) {
            entries_.clear();
            return;
        }

        // Drop any cached entries earlier than current_index
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                           [&](const Entry& e) {
                               for (std::size_t i = current_index; i < album_files.size(); ++i) {
                                   if (album_files[i] == e.path) {
                                       return false;
                                   }
                               }
                               return true;
                           }),
            entries_.end());

        const std::size_t target_end =
            (policy_.mode == PreloadMode::WholeAlbum)
                ? album_files.size()
                : std::min<std::size_t>(album_files.size(), current_index + policy_.max_window_files);

        for (std::size_t idx = current_index; idx < target_end; ++idx) {
            if (entries_.size() >= policy_.max_window_files) {
                break;
            }
            const std::string& p = album_files[idx];
            if (HasEntry(p)) {
                continue;
            }
            std::vector<std::uint8_t> file_bytes;
            if (!ReadFileToMemory(p, file_bytes)) {
                continue;
            }
            if (policy_.max_total_bytes > 0 &&
                (TotalBytes() + file_bytes.size()) > policy_.max_total_bytes) {
                break;
            }
            entries_.push_back(Entry{p, std::move(file_bytes)});
        }
    }

    const Entry* Find(std::string_view path) const noexcept {
        for (const auto& e : entries_) {
            if (e.path == path) {
                return &e;
            }
        }
        return nullptr;
    }

    std::size_t CachedFileCount() const noexcept { return entries_.size(); }

    std::size_t TotalBytes() const noexcept {
        std::size_t sum = 0;
        for (const auto& e : entries_) {
            sum += e.bytes.size();
        }
        return sum;
    }

private:
    bool HasEntry(std::string_view path) const noexcept {
        return Find(path) != nullptr;
    }

    static bool ReadFileToMemory(const std::string& path, std::vector<std::uint8_t>& out) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            return false;
        }
        const auto end_pos = in.tellg();
        if (end_pos <= 0) {
            return false;
        }
        const auto size = static_cast<std::size_t>(end_pos);
        out.resize(size);
        in.seekg(0, std::ios::beg);
        return static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()),
                                       static_cast<std::streamsize>(size)));
    }

    PreloadPolicy policy_;
    std::vector<Entry> entries_;
};

} // namespace flac_ax
