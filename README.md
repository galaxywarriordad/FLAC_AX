# 🎛️ FLAC_AX — Deterministic `[AX]` Audio/Visual Studio & ESS Sabre Appliance

`FLAC_AX` provides a unified audio/visual appliance combining a bit-perfect high-resolution player (`flac_ax`) with a real-time polyphonic synthesizer, tactile mixer, and OpenGL HUD (`ax_av_studio`) on `Linux 6.12-rt` (`Piccolo [AX]`).

---

## 🏗️ `2:3:1` Hardware & Signal Topology

* **Audio Clock Domain (Core 0):** `SCHED_FIFO` direct ALSA `S32_LE` writer and USB xHCI IRQ for the ESS Sabre DAC.
* **Video Clock Domain (Core 1):** VBlank-locked OpenGL studio HUD rendering the FFT spectrum, oscilloscope, and MIDI grid.
* **DSP & Synthesis Domain (Cores 2–4):** 16-voice polyphonic synthesizer, filter, tactile mixer, and FFT tap.
* **Control & Storage Domain (Core 5):** Non-blocking ALSA RawMIDI listener for the Akai MPK Mini MK3 and preloader.

---

## 📂 Repository & Module Overview

* **`src/main.cpp` & `studio_main.cpp`:** Core player and unified A/V Studio entry points.
* **`src/ess_dac_hooks.hpp` & `preload_policy.hpp`:** Hardware profile checks and adaptive RAM preloading policies.
* **`src/midi/`, `src/dsp/`, & `src/visual/`:** Lock-free event rings, AVX2-accelerated synth/FFT, and OpenGL HUD components.

### Akai MPK Mini MK3 Control Map (`ax_av_studio`)
* **25 Keys & 8 MPC Pads:** Polyphonic voice triggers with velocity and sustain control.
* **Thumbstick & Knobs `K1–K8`:** Pitch/modulation wheel and real-time synthesis parameters.

---

## 🔨 Build & Run

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON
cmake --build build
```

* **Bit-Perfect Player (`flac_ax`):** `./build/flac_ax /path/to/track.flac --device hw:CARD=FC3,DEV=0`
* **Live A/V Studio (`ax_av_studio`):** `./build/ax_av_studio --device hw:CARD=FC3,DEV=0 --rate 48000 --period 96`

---

## 🔗 The Silicon Preservation Society — Official Repository Network

For canonical repository links and upstream project references, please visit the official [GitHub repository](https://github.com).

## License

MIT — see [LICENSE](LICENSE).
