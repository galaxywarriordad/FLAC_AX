#include "persist/ax_persist.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace flac_ax::persist {

[[nodiscard]] bool SaveStudioPatchAtomic(
    std::string_view patch_name,
    std::string_view patch_data) noexcept {
    const fs::path path =
        fs::path("/userdata/system/configs/flac_ax/patches") / (std::string(patch_name) + ".axpatch");
    return SavePresetOrPlaylistAtomic(path, patch_data);
}

[[nodiscard]] bool SavePlaylistAtomic(
    std::string_view playlist_name,
    std::string_view m3u_or_json_data) noexcept {
    const fs::path path =
        fs::path("/userdata/music/playlists") / (std::string(playlist_name) + ".m3u8");
    return SavePresetOrPlaylistAtomic(path, m3u_or_json_data);
}

[[nodiscard]] bool SaveUpscalerPresetAtomic(
    std::string_view preset_name,
    std::string_view preset_data) noexcept {
    const fs::path path =
        fs::path("/userdata/system/configs/flac_ax/upscaler") / (std::string(preset_name) + ".cfg");
    return SavePresetOrPlaylistAtomic(path, preset_data);
}

} // namespace flac_ax::persist
```[cite: 21]

#### 2. `DsdDopStreamer::ReadDsfBlockFrames` scratch reuse (`src/dsd_dop_reader.hpp`)
Add `std::vector<std::uint8_t> dsf_block_scratch_;` to `DsdDopStreamer`'s private members and replace `std::vector<std::uint8_t> blk(total_block_bytes);` with:
```cpp
if (dsf_block_scratch_.size() != total_block_bytes) {
    dsf_block_scratch_.resize(total_block_bytes);
}
const std::size_t got = ReadRawBytes(block_rel_off, dsf_block_scratch_.data(), dsf_block_scratch_.size());
