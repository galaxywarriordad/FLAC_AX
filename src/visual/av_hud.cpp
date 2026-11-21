// src/visual/av_hud.cpp — VBlank-locked OpenGL FFT spectrum, phase scope & MPK Mini MK3 HUD
#include "visual/av_hud.hpp"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace flac_ax::visual {

namespace {

void DrawRect(float x0, float y0, float x1, float y1, float r, float g, float b, float a = 1.0f) noexcept {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

void DrawRectOutline(float x0, float y0, float x1, float y1, float r, float g, float b) noexcept {
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

} // namespace

AvStudioHud::~AvStudioHud() {
    Shutdown();
}

bool AvStudioHud::Init(int core_id, bool fullscreen) {
    if (core_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned int>(core_id), &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[HUD] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_Window* win = SDL_CreateWindow(
        "FLAC_AX // [AX] Audio-Visual Studio (ESS Sabre + Akai MPK Mini MK3)",
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

    SDL_GL_SetSwapInterval(1); // VBlank FIFO lock (60/120 Hz)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
    SDL_Quit();
}

bool AvStudioHud::RenderFrame(const dsp::VisualSnapshot& snap,
                              const midi::MpkMini3State& mpk,
                              const StudioTelemetry& telem) {
    if (!window_ || !gl_ctx_) {
        return false;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            return false;
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            return false;
        }
    }

    SDL_GetWindowSize(static_cast<SDL_Window*>(window_), &width_, &height_);
    glViewport(0, 0, width_, height_);

    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 1. Top Status Bar (DAC Lock, MPK Mini MK3 Connection, Active Voices)
    DrawRect(-0.96f, 0.86f, 0.96f, 0.95f, 0.08f, 0.11f, 0.16f, 0.95f);
    DrawRectOutline(-0.96f, 0.86f, 0.96f, 0.95f, 0.18f, 0.55f, 0.85f);

    // Connection indicator pills: Left = ESS Sabre Lock (Cyan), Right = MPK Mini MK3 (Emerald/Amber)
    DrawRect(-0.94f, 0.88f, -0.88f, 0.93f, 0.10f, 0.85f, 0.95f);
    if (telem.mpk_connected) {
        DrawRect(0.88f, 0.88f, 0.94f, 0.93f, 0.15f, 0.92f, 0.45f);
    } else {
        DrawRect(0.88f, 0.88f, 0.94f, 0.93f, 0.95f, 0.65f, 0.15f);
    }

    // Active polyphonic voice LEDs (16 slots across top bar)
    for (std::uint32_t v = 0; v < 16; ++v) {
        const float vx0 = -0.50f + static_cast<float>(v) * 0.062f;
        const float vx1 = vx0 + 0.048f;
        const bool lit = (v < snap.active_voices);
        DrawRect(vx0, 0.885f, vx1, 0.925f,
                 lit ? 0.20f : 0.12f,
                 lit ? 0.90f : 0.16f,
                 lit ? 0.65f : 0.22f);
    }

    // 2. Left Panel: 64-Band Log-Spaced Real-Time FFT Spectrum Analyzer
    DrawRect(-0.96f, 0.05f, 0.28f, 0.82f, 0.06f, 0.08f, 0.12f, 0.95f);
    DrawRectOutline(-0.96f, 0.05f, 0.28f, 0.82f, 0.15f, 0.35f, 0.55f);

    constexpr std::size_t kBins = dsp::VisualSnapshot::kSpectrumBins;
    const float bar_span = 1.20f / static_cast<float>(kBins);
    for (std::size_t b = 0; b < kBins; ++b) {
        const float mag = std::clamp(snap.spectrum[b], 0.0f, 1.0f);
        const float bx0 = -0.94f + static_cast<float>(b) * bar_span;
        const float bx1 = bx0 + bar_span * 0.78f;
        const float by0 = 0.07f;
        const float by1 = by0 + mag * 0.72f;
        DrawRect(bx0, by0, bx1, by1,
                 0.12f + 0.75f * mag,
                 0.85f - 0.35f * mag,
                 0.95f - 0.55f * mag);
    }

    // 3. Right Panel: Stereo Time-Domain Oscilloscope + L/R Peak Meters
    DrawRect(0.32f, 0.05f, 0.96f, 0.82f, 0.06f, 0.08f, 0.12f, 0.95f);
    DrawRectOutline(0.32f, 0.05f, 0.96f, 0.82f, 0.15f, 0.35f, 0.55f);

    // Stereo L/R Peak Meters
    DrawRect(0.86f, 0.08f, 0.89f, 0.08f + std::clamp(snap.peak_l, 0.0f, 1.0f) * 0.70f, 0.15f, 0.90f, 0.50f);
    DrawRect(0.91f, 0.08f, 0.94f, 0.08f + std::clamp(snap.peak_r, 0.0f, 1.0f) * 0.70f, 0.15f, 0.90f, 0.50f);

    // Waveform Trace (Left = Cyan, Right = Magenta)
    constexpr std::size_t kScope = dsp::VisualSnapshot::kScopeSamples;
    glLineWidth(2.0f);
    glColor3f(0.15f, 0.90f, 0.95f);
    glBegin(GL_LINE_STRIP);
    for (std::size_t i = 0; i < kScope; ++i) {
        const float x = 0.34f + (static_cast<float>(i) / static_cast<float>(kScope - 1)) * 0.49f;
        const float y = 0.44f + std::clamp(snap.scope_l[i], -1.0f, 1.0f) * 0.34f;
        glVertex2f(x, y);
    }
    glEnd();

    glColor3f(0.92f, 0.35f, 0.80f);
    glBegin(GL_LINE_STRIP);
    for (std::size_t i = 0; i < kScope; ++i) {
        const float x = 0.34f + (static_cast<float>(i) / static_cast<float>(kScope - 1)) * 0.49f;
        const float y = 0.44f + std::clamp(snap.scope_r[i], -1.0f, 1.0f) * 0.34f;
        glVertex2f(x, y);
    }
    glEnd();

    // 4. Bottom Panel: Akai MPK Mini MK3 Tactile Surface (8 Pads Left + 8 Rotary Knobs K1..K8 Right)
    DrawRect(-0.96f, -0.94f, 0.96f, 0.00f, 0.06f, 0.08f, 0.12f, 0.95f);
    DrawRectOutline(-0.96f, -0.94f, 0.96f, 0.00f, 0.15f, 0.35f, 0.55f);

    // 2x4 MPC Pad Grid (Pads 1..8)
    for (int p = 0; p < 8; ++p) {
        const int col = p % 4;
        const int row = p / 4; // 0 = top row (Pads 5..8), 1 = bottom row (Pads 1..4)
        const float px0 = -0.92f + static_cast<float>(col) * 0.21f;
        const float px1 = px0 + 0.18f;
        const float py1 = -0.08f - static_cast<float>(row) * 0.42f;
        const float py0 = py1 - 0.36f;

        const float vel = static_cast<float>(mpk.pad_vel[static_cast<std::size_t>(p)].load(std::memory_order_relaxed)) / 127.0f;
        DrawRect(px0, py0, px1, py1,
                 0.14f + 0.82f * vel,
                 0.16f + 0.25f * vel,
                 0.20f + 0.15f * vel);
        DrawRectOutline(px0, py0, px1, py1, 0.85f, 0.22f, 0.28f);
    }

    // 2x4 Rotary Encoder Bars (K1..K8 = CC 70..77)
    for (int k = 0; k < 8; ++k) {
        const int col = k % 4;
        const int row = k / 4;
        const float kx0 = 0.04f + static_cast<float>(col) * 0.22f;
        const float kx1 = kx0 + 0.18f;
        const float ky1 = -0.10f - static_cast<float>(row) * 0.42f;
        const float ky0 = ky1 - 0.32f;

        const float norm = static_cast<float>(mpk.knobs[static_cast<std::size_t>(k)].load(std::memory_order_relaxed)) / 127.0f;
        DrawRect(kx0, ky0, kx1, ky1, 0.10f, 0.13f, 0.18f);
        DrawRect(kx0, ky0, kx0 + (kx1 - kx0) * norm, ky1, 0.18f, 0.78f, 0.95f);
        DrawRectOutline(kx0, ky0, kx1, ky1, 0.25f, 0.55f, 0.75f);
    }

    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
    return true;
}

} // namespace flac_ax::visual
