// src/visual/av_hud.cpp — Core 1 GTX 980 Ti VBlank-locked OpenGL HUD implementation
#include "visual/av_hud.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace flac_ax::visual {

namespace {

void PinHudThread(int core_id) noexcept {
    if (core_id < 0) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned int>(core_id), &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

void DrawRect(float x0, float y0, float x1, float y1, float r, float g, float b, float a = 1.0f) noexcept {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

void DrawLineStripScope(const std::array<float, dsp::VisualSnapshot::kScopeSamples>& samples,
                        float x0, float y_center, float width, float half_height,
                        float r, float g, float b) noexcept {
    constexpr std::size_t N = dsp::VisualSnapshot::kScopeSamples;
    if (N < 2 || width <= 0.0f) {
        return;
    }
    glColor4f(r, g, b, 0.95f);
    glBegin(GL_LINE_STRIP);
    for (std::size_t i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N - 1);
        const float x = x0 + t * width;
        const float v = std::clamp(samples[i], -1.25f, 1.25f);
        const float y = y_center + (v * half_height);
        glVertex2f(x, y);
    }
    glEnd();
}

} // namespace

AvStudioHud::~AvStudioHud() {
    Shutdown();
}

bool AvStudioHud::Init(int core_id, bool fullscreen) {
    Shutdown();
    PinHudThread(core_id);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[HUD] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_Window* win = SDL_CreateWindow(
        "FLAC_AX // ax_av_studio [Core 1 VBlank HUD]",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width_,
        height_,
        flags);
    if (!win) {
        std::fprintf(stderr, "[HUD] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "[HUD] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(win, ctx);
    if (SDL_GL_SetSwapInterval(1) != 0) {
        SDL_GL_SetSwapInterval(0);
    }

    SDL_GetWindowSize(win, &width_, &height_);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.5f);

    window_ = win;
    gl_ctx_ = ctx;
    return true;
}

void AvStudioHud::Shutdown() {
    if (gl_ctx_) {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_ctx_));
        gl_ctx_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }
}

bool AvStudioHud::RenderFrame(const dsp::VisualSnapshot& snap,
                              const midi::MpkMini3State& mpk,
                              const StudioTelemetry& telem) {
    auto* win = static_cast<SDL_Window*>(window_);
    if (!win || !gl_ctx_) {
        return false;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            return false;
        }
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            width_ = std::max(320, ev.window.data1);
            height_ = std::max(200, ev.window.data2);
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            return false;
        }
    }

    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);

    glViewport(0, 0, width_, height_);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(w), static_cast<double>(h), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const float pad = 16.0f;
    const float top_h = 28.0f;

    // Top status strip: DAC lock, MPK3 status, active voices & XRUNs
    DrawRect(pad, pad, pad + 14.0f, pad + 14.0f,
             telem.dac_locked ? 0.15f : 0.75f, telem.dac_locked ? 0.85f : 0.20f, 0.30f);
    DrawRect(pad + 24.0f, pad, pad + 38.0f, pad + 14.0f,
             mpk.connected.load(std::memory_order_relaxed) ? 0.20f : 0.45f,
             mpk.connected.load(std::memory_order_relaxed) ? 0.70f : 0.45f, 0.95f);

    const float voice_ratio = std::clamp(static_cast<float>(snap.active_voices) / 16.0f, 0.0f, 1.0f);
    DrawRect(pad + 54.0f, pad, pad + 54.0f + voice_ratio * 180.0f, pad + 14.0f, 0.95f, 0.65f, 0.15f);

    if (telem.xruns > 0) {
        DrawRect(w - pad - 18.0f, pad, w - pad, pad + 14.0f, 0.95f, 0.20f, 0.20f);
    }

    // 64-band FFT spectrum panel
    const float spec_x0 = pad;
    const float spec_y0 = pad + top_h + 8.0f;
    const float spec_w = w - 2.0f * pad;
    const float spec_h = h * 0.42f;
    DrawRect(spec_x0, spec_y0, spec_x0 + spec_w, spec_y0 + spec_h, 0.09f, 0.11f, 0.14f);

    constexpr std::size_t kBins = dsp::VisualSnapshot::kSpectrumBins;
    const float gap = 2.0f;
    const float bar_w = std::max(2.0f, (spec_w - gap * static_cast<float>(kBins + 1)) / static_cast<float>(kBins));
    for (std::size_t b = 0; b < kBins; ++b) {
        const float mag = std::clamp(snap.spectrum[b], 0.0f, 1.0f);
        const float bh = mag * (spec_h - 12.0f);
        const float bx0 = spec_x0 + gap + static_cast<float>(b) * (bar_w + gap);
        const float by1 = spec_y0 + spec_h - 6.0f;
        const float by0 = by1 - bh;
        DrawRect(bx0, by0, bx0 + bar_w, by1, 0.20f + 0.75f * mag, 0.80f - 0.35f * mag, 0.45f);
    }

    // Lower split: L/R oscilloscope (left) + K1..K8 knobs & 16 pads (right)
    const float bot_y0 = spec_y0 + spec_h + 14.0f;
    const float bot_h = std::max(80.0f, (h - pad) - bot_y0);
    const float half_w = (spec_w - 14.0f) * 0.5f;

    DrawRect(spec_x0, bot_y0, spec_x0 + half_w, bot_y0 + bot_h, 0.09f, 0.11f, 0.14f);
    const float mid_y = bot_y0 + bot_h * 0.5f;
    DrawLineStripScope(snap.scope_l, spec_x0 + 8.0f, mid_y, half_w - 16.0f, bot_h * 0.36f, 0.25f, 0.90f, 0.95f);
    DrawLineStripScope(snap.scope_r, spec_x0 + 8.0f, mid_y, half_w - 16.0f, bot_h * 0.36f, 0.95f, 0.55f, 0.25f);

    const float right_x0 = spec_x0 + half_w + 14.0f;
    DrawRect(right_x0, bot_y0, right_x0 + half_w, bot_y0 + bot_h, 0.09f, 0.11f, 0.14f);

    // K1..K8 bars
    const float knob_w = std::max(8.0f, (half_w - 24.0f) / 8.0f);
    const float knob_area_h = bot_h * 0.45f;
    for (std::size_t k = 0; k < 8; ++k) {
        const float kv = static_cast<float>(mpk.knobs[k].load(std::memory_order_relaxed)) / 127.0f;
        const float kx0 = right_x0 + 10.0f + static_cast<float>(k) * knob_w;
        const float ky1 = bot_y0 + 12.0f + knob_area_h;
        const float ky0 = ky1 - std::clamp(kv, 0.0f, 1.0f) * (knob_area_h - 8.0f);
        DrawRect(kx0 + 2.0f, ky0, kx0 + knob_w - 4.0f, ky1, 0.85f, 0.75f, 0.20f);
    }

    // 16 pad indicators (2 rows x 8 cols)
    const float pad_grid_y0 = bot_y0 + knob_area_h + 22.0f;
    const float pad_cell_h = std::max(10.0f, (bot_y0 + bot_h - 10.0f - pad_grid_y0) * 0.5f - 4.0f);
    for (std::size_t p = 0; p < 16; ++p) {
        const std::size_t row = p / 8;
        const std::size_t col = p % 8;
        const float pv = static_cast<float>(mpk.pad_vel[p].load(std::memory_order_relaxed)) / 127.0f;
        const float px0 = right_x0 + 10.0f + static_cast<float>(col) * knob_w;
        const float py0 = pad_grid_y0 + static_cast<float>(row) * (pad_cell_h + 6.0f);
        DrawRect(px0 + 2.0f, py0, px0 + knob_w - 4.0f, py0 + pad_cell_h,
                 0.20f + 0.75f * pv, 0.35f + 0.55f * pv, 0.90f);
    }

    SDL_GL_SwapWindow(win);
    return true;
}

} // namespace flac_ax::visual
