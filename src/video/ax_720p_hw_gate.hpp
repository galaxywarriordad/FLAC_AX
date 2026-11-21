// src/video/ax_720p_hw_gate.hpp — Consumes ax-sdk hardware_profiles.json guardrails + optional DLL probe
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ax::video {

inline constexpr unsigned int kTargetWidth720p = 1280U;
inline constexpr unsigned int kTargetHeight720p = 720U;

struct UpscaleHwGuardrails {
    double vram_ceiling_gib{5.50};     // Matches maxwell_gtx980ti default in hardware_profiles.json
    double vram_reap_trigger_gib{5.00};
    std::uint64_t storage_aux_mask{0x20ULL};
    bool has_optional_dll{false};
    std::filesystem::path dll_path;
};

// Populates 720p guardrails from resolved ax-sdk profile values and optionally probes `dll_folder`.
// Always falls back cleanly to the native 720p shader pipeline when no DLL is present or usable.
inline UpscaleHwGuardrails Resolve720pGuardrails(double profile_vram_ceiling_gib,
                                                  double profile_vram_reap_gib,
                                                  std::uint64_t storage_aux_mask,
                                                  const std::filesystem::path& dll_folder = {}) {
    UpscaleHwGuardrails cfg{};
    cfg.vram_ceiling_gib = (profile_vram_ceiling_gib > 0.0) ? profile_vram_ceiling_gib : 5.50;
    cfg.vram_reap_trigger_gib = (profile_vram_reap_gib > 0.0) ? profile_vram_reap_gib : 5.00;
    cfg.storage_aux_mask = (storage_aux_mask != 0ULL) ? storage_aux_mask : 0x20ULL;

    std::error_code ec;
    if (!dll_folder.empty() && std::filesystem::is_directory(dll_folder, ec)) {
        const auto candidate = dll_folder / "Lossless.dll";
        if (std::filesystem::is_regular_file(candidate, ec) && !std::filesystem::is_symlink(candidate, ec)) {
#if defined(_WIN32)
            cfg.has_optional_dll = true;
            cfg.dll_path = candidate;
#else
            // Native Linux (Piccolo [AX]) uses the native 720p shader path rather than loading a Windows DLL
            cfg.has_optional_dll = false;
#endif
        }
    }
    return cfg;
}

} // namespace ax::video
