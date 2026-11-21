// src/video/upscale_720p_shader.hpp — 720p (1280x720) GLSL shader & pipeline state for FLAC_AX
#pragma once

#include "ax_720p_hw_gate.hpp"

#include <cstdint>
#include <string_view>

namespace ax::video {

enum class ScanMode : std::uint8_t {
    Progressive240p = 0,
    BobInterlaced480i576i,
    DvdCadenceAdaptive
};

struct Upscale720pParams {
    ScanMode scan_mode{ScanMode::DvdCadenceAdaptive};
    float anti_ringing_strength{0.75f}; // [0.0..1.0] neighborhood clamp blend
    float sharpness{0.35f};             // [0.0..1.0] subtle edge crispness
    float dither_amplitude{0.5f / 255.0f};
    bool is_pal_576{false};
    bool top_field_first{true};
};

// Fragment shader for 720p (1280x720) output:
// - BT.601 limited-range YUV decoding
// - 240p passthrough / 480i-576i single-field Bob / DVD cadence blend
// - Catmull-Rom / cubic upscale with local min/max anti-ringing clamp + dither
inline constexpr std::string_view kUpscale720pFragmentShader = R"GLSL(
#version 330 core

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_y_tex;       // R8 luminance (or YUYV packed in RGBA8/RG8 depending on upload)
uniform sampler2D u_uv_tex;      // RG8 chroma (Cb, Cr)
uniform vec2 u_src_size;         // Source dimensions (e.g., 720x480, 720x576, or 256x240)
uniform vec2 u_dst_size;         // Target output size (1280x720)
uniform int u_scan_mode;         // 0 = 240p progressive, 1 = 480i/576i bob, 2 = DVD adaptive
uniform int u_field_parity;      // 0 = even field, 1 = odd field
uniform float u_anti_ringing;    // 0.0 .. 1.0
uniform float u_dither_amp;      // e.g., 0.5/255.0

vec3 YuvToRgbBt601(float y_raw, float u_raw, float v_raw) {
    float y = max(0.0, (y_raw - (16.0 / 255.0)) * (255.0 / 219.0));
    float cb = (u_raw - (128.0 / 255.0)) * (255.0 / 224.0);
    float cr = (v_raw - (128.0 / 255.0)) * (255.0 / 224.0);

    float r = y + 1.4020 * cr;
    float g = y - 0.344136 * cb - 0.714136 * cr;
    float b = y + 1.7720 * cb;
    return max(vec3(0.0), vec3(r, g, b));
}

float SampleLumaWithScan(vec2 uv) {
    vec2 texel = 1.0 / max(u_src_size, vec2(1.0));
    if (u_scan_mode == 1) {
        // Bob deinterlace: keep active field scanlines, interpolate missing scanline
        float y_px = uv.y * u_src_size.y;
        float row = floor(y_px);
        int is_odd_row = int(mod(row, 2.0));
        if (is_odd_row == u_field_parity) {
            return texture(u_y_tex, uv).r;
        }
        float above = texture(u_y_tex, vec2(uv.x, clamp((row - 1.0 + 0.5) * texel.y, 0.0, 1.0))).r;
        float below = texture(u_y_tex, vec2(uv.x, clamp((row + 1.0 + 0.5) * texel.y, 0.0, 1.0))).r;
        return 0.5 * (above + below);
    }
    return texture(u_y_tex, uv).r;
}

vec3 SampleRgbAt(vec2 uv) {
    float y = SampleLumaWithScan(uv);
    vec2 chroma = texture(u_uv_tex, uv).rg;
    return YuvToRgbBt601(y, chroma.r, chroma.g);
}

// Mitchell-Netravali cubic weights (B = 1/3, C = 1/3) for clean 720p upscaling
float CubicWeight(float x) {
    x = abs(x);
    if (x < 1.0) {
        return ((12.0 - 9.0 * (1.0 / 3.0) - 6.0 * (1.0 / 3.0)) * x * x * x +
                (-18.0 + 12.0 * (1.0 / 3.0) + 6.0 * (1.0 / 3.0)) * x * x +
                (6.0 - 2.0 * (1.0 / 3.0))) / 6.0;
    }
    if (x < 2.0) {
        return ((-1.0 / 3.0 - 6.0 * (1.0 / 3.0)) * x * x * x +
                (6.0 * (1.0 / 3.0) + 30.0 * (1.0 / 3.0)) * x * x +
                (-12.0 * (1.0 / 3.0) - 48.0 * (1.0 / 3.0)) * x +
                (8.0 * (1.0 / 3.0) + 24.0 * (1.0 / 3.0))) / 6.0;
    }
    return 0.0;
}

float InterleavedGradientNoise(vec2 pos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

void main() {
    vec2 src_pos = v_uv * u_src_size - 0.5;
    vec2 base_px = floor(src_pos);
    vec2 frac_px = src_pos - base_px;
    vec2 inv_src = 1.0 / max(u_src_size, vec2(1.0));

    vec3 accum = vec3(0.0);
    float w_sum = 0.0;
    vec3 lo = vec3(1e9);
    vec3 hi = vec3(-1e9);

    for (int j = -1; j <= 2; ++j) {
        float wy = CubicWeight(float(j) - frac_px.y);
        for (int i = -1; i <= 2; ++i) {
            float wx = CubicWeight(float(i) - frac_px.x);
            float w = wx * wy;
            vec2 sample_uv = clamp((base_px + vec2(float(i), float(j)) + 0.5) * inv_src,
                                   vec2(0.0), vec2(1.0));
            vec3 c = SampleRgbAt(sample_uv);
            accum += c * w;
            w_sum += w;

            // Track 3x3 local neighborhood bounds around center for anti-ringing
            if (abs(i) <= 1 && abs(j) <= 1) {
                lo = min(lo, c);
                hi = max(hi, c);
            }
        }
    }

    vec3 filtered = (w_sum > 1e-5) ? (accum / w_sum) : SampleRgbAt(v_uv);
    vec3 clamped = clamp(filtered, lo, hi);
    vec3 out_rgb = mix(filtered, clamped, clamp(u_anti_ringing, 0.0, 1.0));

    // Subtle spatial dither to reduce banding on smooth gradients
    float ign = InterleavedGradientNoise(gl_FragCoord.xy) - 0.5;
    out_rgb = max(vec3(0.0), out_rgb + vec3(ign * u_dither_amp));

    frag_color = vec4(out_rgb, 1.0);
}
)GLSL";

} // namespace ax::video
