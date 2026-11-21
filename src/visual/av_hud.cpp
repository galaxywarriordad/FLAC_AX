// src/visual/av_hud.hpp — Core 1 GTX 980 Ti VBlank-locked OpenGL FFT spectrum, phase scope & MPK Mini MK3 HUD
#pragma once

#include "dsp/avx2_synth.hpp"
#include "midi/mpk_mini3.hpp"

#include <cstdint>

namespace flac_ax::visual {

struct StudioTelemetry {
    unsigned int sample_rate_hz{48000};
    unsigned int period_frames{96};
    std::uint64_t xruns{0};
    std::uint32_t jitter_p99_us{0};
    bool dac_locked{false};
    bool mpk_connected{false};
};

class AvStudioHud {
public:
    AvStudioHud() = default;
    ~AvStudioHud();

    AvStudioHud(const AvStudioHud&) = delete;
    AvStudioHud& operator=(const AvStudioHud&) = delete;

    // Pins rendering thread to Core 1 (0x02 / VulkanSubmission domain) & opens VBlank-locked OpenGL window
    bool Init(int core_id = 1, bool fullscreen = false);
    void Shutdown();

    // Polls SDL2 events (returns false on window close or ESC) and renders one VBlank-locked HUD frame
    bool RenderFrame(const dsp::VisualSnapshot& snap,
                     const midi::MpkMini3State& mpk,
                     const StudioTelemetry& telem);

private:
    void* window_{nullptr};
    void* gl_ctx_{nullptr};
    int width_{1280};
    int height_{720};
};

} // namespace flac_ax::visual
