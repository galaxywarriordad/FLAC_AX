#pragma once

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace flac_ax {

enum class PreloadMode {
    SingleFile,
    Window3Files,
    WholeAlbum
};

inline constexpr const char* ToString(PreloadMode mode) noexcept {
    switch (mode) {
        case PreloadMode::WholeAlbum:
            return "WholeAlbum";
        case PreloadMode::Window3Files:
            return "Window3Files";
        case PreloadMode::SingleFile:
        default:
            return "SingleFile";
    }
}

struct PreloadPolicy {
    PreloadMode mode{PreloadMode::SingleFile};
    std::size_t max_window_files{1};
    std::size_t max_total_bytes{0};
    std::uint64_t detected_mem_total_bytes{0};
    std::uint64_t detected_mem_available_bytes{0};
};

inline void ReadLinuxMemInfo(std::uint64_t& mem_total_bytes,
                             std::uint64_t& mem_available_bytes) noexcept {
    mem_total_bytes = 0;
    mem_available_bytes = 0;

#if !defined(_WIN32)
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
        std::getline(meminfo, unit);
        if (mem_total_bytes > 0 && mem_available_bytes > 0) {
            break;
        }
    }
#endif
}

inline PreloadPolicy EvaluatePreloadPolicy(
    std::uint64_t mem_total_bytes,
    std::uint64_t mem_available_bytes = 0) noexcept {
    constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
    constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kWholeAlbumThreshold = 16ULL * kGiB;
    constexpr std::uint64_t kEightGbTierMin = (6ULL * kGiB) + (512ULL * kMiB);
    constexpr std::uint64_t kEmbeddedHandheldMax = (1ULL * kGiB) + (512ULL * kMiB);

    PreloadPolicy policy{};
    policy.detected_mem_total_bytes = mem_total_bytes;
    policy.detected_mem_available_bytes = mem_available_bytes;

    if (mem_total_bytes >= kWholeAlbumThreshold) {
        policy.mode = PreloadMode::WholeAlbum;
        policy.max_window_files = (std::numeric_limits<std::size_t>::max)();
        policy.max_total_bytes = static_cast<std::size_t>(8ULL * kGiB);
    } else if (mem_total_bytes >= kEightGbTierMin) {
        policy.mode = PreloadMode::Window3Files;
        policy.max_window_files = 3;
        if (mem_available_bytes > 0 && mem_available_bytes < (2ULL * kGiB)) {
            policy.max_window_files = 1;
            policy.mode = PreloadMode::SingleFile;
        }
        policy.max_total_bytes = static_cast<std::size_t>(2ULL * kGiB);
    } else if (mem_total_bytes > 0 && mem_total_bytes <= kEmbeddedHandheldMax) {
        policy.mode = PreloadMode::SingleFile;
        policy.max_window_files = 1;
        policy.max_total_bytes = static_cast<std::size_t>(64ULL * kMiB);
    } else {
        policy.mode = PreloadMode::SingleFile;
        policy.max_window_files = 1;
        policy.max_total_bytes = static_cast<std::size_t>(512ULL * kMiB);
    }

    return policy;
}

inline PreloadPolicy DetectHostPreloadPolicy() noexcept {
    std::uint64_t total = 0;
    std::uint64_t avail = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX statex{};
    statex.dwLength = sizeof(statex);
    if (::GlobalMemoryStatusEx(&statex)) {
        total = static_cast<std::uint64_t>(statex.ullTotalPhys);
        avail = static_cast<std::uint64_t>(statex.ullAvailPhys);
    }
#else
    ReadLinuxMemInfo(total, avail);
#endif
    return EvaluatePreloadPolicy(total, avail);
}

class RamFilePreloader {
public:
    struct Entry {
        std::string path;
        std::vector<std::uint8_t> bytes;
    };

    explicit RamFilePreloader(PreloadPolicy policy)
        : policy_(policy) {}

    const PreloadPolicy& policy() const noexcept { return policy_; }

    void RefreshWindow(const std::vector<std::string>& album_files, std::size_t current_index) {
        if (current_index >= album_files.size()) {
            entries_.clear();
            total_bytes_ = 0;
            return;
        }

        const std::size_t target_end =
            (policy_.mode == PreloadMode::WholeAlbum)
                ? album_files.size()
                : std::min<std::size_t>(album_files.size(), current_index + policy_.max_window_files);

        std::unordered_set<std::string_view> keep_set;
        keep_set.reserve(target_end - current_index);
        for (std::size_t i = current_index; i < target_end; ++i) {
            keep_set.insert(album_files[i]);
        }

        std::size_t write_idx = 0;
        for (std::size_t read_idx = 0; read_idx < entries_.size(); ++read_idx) {
            if (keep_set.find(entries_[read_idx].path) != keep_set.end()) {
                if (write_idx != read_idx) {
                    entries_[write_idx] = std::move(entries_[read_idx]);
                }
                ++write_idx;
            } else {
                total_bytes_ -= entries_[read_idx].bytes.size();
            }
        }
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(write_idx), entries_.end());

        for (std::size_t idx = current_index; idx < target_end; ++idx) {
            if (entries_.size() >= policy_.max_window_files) {
                break;
            }
            const std::string& p = album_files[idx];
            if (Find(p) != nullptr) {
                continue;
            }
            std::vector<std::uint8_t> file_bytes;
            if (!ReadFileToMemory(p, file_bytes)) {
                continue;
            }
            if (policy_.max_total_bytes > 0 &&
                (total_bytes_ + file_bytes.size()) > policy_.max_total_bytes) {
                break;
            }
            total_bytes_ += file_bytes.size();
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
    std::size_t TotalBytes() const noexcept { return total_bytes_; }

private:
    static bool ReadFileToMemory(const std::string& path, std::vector<std::uint8_t>& out) {
#if defined(_WIN32)
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
        const auto end_pos = file.tellg();
        if (end_pos <= 0) {
            return false;
        }
        const auto file_size = static_cast<std::size_t>(end_pos);
        file.seekg(0, std::ios::beg);
        out.resize(file_size);
        if (!file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(file_size))) {
            out.clear();
            return false;
        }
        return true;
#else
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return false;
        }

        struct stat st {};
        if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
            ::close(fd);
            return false;
        }

        const auto file_size = static_cast<std::size_t>(st.st_size);
        out.resize(file_size);

        std::size_t offset = 0;
        while (offset < file_size) {
            const ssize_t n = ::read(fd, out.data() + offset, file_size - offset);
            if (n > 0) {
                offset += static_cast<std::size_t>(n);
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                ::close(fd);
                out.clear();
                return false;
            }
        }

        ::close(fd);
        return true;
#endif
    }

    PreloadPolicy policy_;
    std::vector<Entry> entries_;
    std::size_t total_bytes_{0};
};

} // namespace flac_ax
