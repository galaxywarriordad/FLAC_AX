#include "persist/ax_persist.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace flac_ax::persist {

bool SaveStudioPatchAtomic(
    std::string_view patch_name,
    std::string_view patch_data) noexcept {
    const fs::path path =
        fs::path("/userdata/system/configs/flac_ax/patches") / (std::string(patch_name) + ".axpatch");
    return SavePresetOrPlaylistAtomic(path, patch_data);
}

bool SavePlaylistAtomic(
    std::string_view playlist_name,
    std::string_view m3u_or_json_data) noexcept {
    const fs::path path =
        fs::path("/userdata/music/playlists") / (std::string(playlist_name) + ".m3u8");
    return SavePresetOrPlaylistAtomic(path, m3u_or_json_data);
}

bool SaveUpscalerPresetAtomic(
    std::string_view preset_name,
    std::string_view preset_data) noexcept {
    const fs::path path =
        fs::path("/userdata/system/configs/flac_ax/upscaler") / (std::string(preset_name) + ".cfg");
    return SavePresetOrPlaylistAtomic(path, preset_data);
}

} // namespace flac_ax::persist
