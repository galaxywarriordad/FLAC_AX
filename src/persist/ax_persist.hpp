#pragma once

#include "ax/persist.hpp"

#include <filesystem>
#include <string_view>

namespace flac_ax::persist {

inline bool SavePresetOrPlaylistAtomic(
    const std::filesystem::path& file_path,
    std::string_view content) noexcept {
    ax::core::CleanupStaleTempFiles(file_path.parent_path(), ".ax_tmp_");

    ax::core::AtomicWriteOptions opts{};
    opts.file_mode = 0644;
    opts.create_parent_dirs = true;
    opts.fsync_parent_dir = true;

    return ax::core::WriteFileAtomic(file_path, content, opts).ok;
}

} // namespace flac_ax::persist
