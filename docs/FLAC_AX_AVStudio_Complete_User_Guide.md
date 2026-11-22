# 🎛️ FLAC_AX & AX AV Studio — Complete User Guide & Build Manual

---

## 🚀 1. Introduction: Why FLAC_AX Exists & How It Stacks Up

Modern desktop operating systems treat audio as a background convenience service. Even on purpose-built workstations, general-purpose audio stacks route sound through software mixers, sample-rate converters, dynamic thread schedulers, and power-managed USB hubs—introducing jitter, buffer underruns, and silent bit-depth truncation. Meanwhile, commercial audiophile players and standalone hardware synthesizers force a binary choice: either pay ongoing subscription fees for heavy desktop apps tethered to bloated operating systems, or spend thousands of pounds on dedicated proprietary DSP boxes with closed firmware and modest embedded CPUs.

**FLAC_AX** (`flac_ax` + `ax_av_studio`) rejects both compromises. Built in zero-dependency C++20 around a deterministic **`2:3:1` physical CPU core topology** on `Linux 6.12-rt` (`Piccolo [AX]`) and standard `x86_64` Linux workstations, it turns proven, off-the-shelf `x86_64` silicon (such as an Intel Core `i5-9600K` or `i5-8600K`) paired with an **ESS Sabre USB DAC** (**HiBy FC3** / `ES9281AC Pro`), a **Nobsound Q10 + Wharfedale Diamond 9.0** desktop speaker rig or **Grado SR325x** open-back headphones, and an **Akai MPK Mini MK3** into a deterministic, bare-metal audiophile transport and `2.00 ms` live synthesizer workstation.

### ✨ What Makes FLAC_AX Special

1. **⚡ Deterministic `2:3:1` Core Partitioning (Zero Scheduler Contention)**
   Instead of letting the OS kernel migrate audio threads across cores alongside UI rendering and disk I/O, `FLAC_AX` pins workloads strictly by role:
   * **Core `0` (Real-Time Audio Clock Domain):** `SCHED_FIFO` (Priority `80`) ALSA `S32_LE` output directly to `hw:CARD=FC3,DEV=0` and USB xHCI IRQ handling—completely isolated from disk reads, heap allocations, and mutexes.
   * **Core `1` (Video & HUD Clock Domain):** VBlank-locked SDL2/OpenGL oscilloscope and 64-band FFT spectrum visualizer (`ax_av_studio`), off-RT MIDI OLED SysEx feedback, and `ax_video_upscaler` frame presentation.
   * **Cores `2–4` (DSP & Synthesis Plane):** 16-voice polyphonic saw/sub synthesizer, 8-voice percussion engine, 2-pole stereo State-Variable Filter, 4-comb `StereoReverbDsp`, and interpolated `TapeDelayDsp`.
   * **Core `5` (Peripheral & Preload I/O Plane):** Whole-album RAM preloading (`RamFilePreloader`), FLAC/DSD decoding, and lock-free ALSA RawMIDI ingestion (`MpkMini3Listener`).

2. **🧠 True RAM-Resident, Zero-Allocation Playback & Synthesis**
   * **`flac_ax` (Bit-Perfect Transport):** Preloads entire FLAC albums and native `.dsf`/`.dff` DSD64/DSD128 tracks into locked RAM (`mlockall`) on Core `5` before streaming raw `S32_LE` frames directly to `hw:CARD=FC3,DEV=0`—eliminating storage bus interrupts, PipeWire/PulseAudio resampling, and software volume scaling.
   * **`ax_av_studio` (Live AV Synthesizer):** Runs a 16-voice polyphonic melodic synth, 8-voice drum engine, 2-pole stereo State-Variable Filter, 4-comb `StereoReverbDsp`, and interpolated `TapeDelayDsp` entirely over preallocated `std::array` buffers with zero heap allocations on the real-time thread.

3. **🔒 Hardware-Level DAC Arbitration & USB Power Tuning**
   Both binaries coordinate exclusive ownership of the ESS Sabre DAC via `/run/lock/ax-dac.lock` (`flock`), preventing accidental sample-rate collisions or device contention while disabling USB autosuspend for `VID 0x32BB` (`power/control="on"`).

---

### 📊 How FLAC_AX Compares to Commercial Software & Hardware

| Dimension | 💿 Commercial Audiophile Players *(Roon / Audirvana Studio)* | 🎹 Commercial Desktop DAWs *(Ableton Live / Bitwig)* | 🎛️ Standalone Hardware Boxes *(Elektron Octatrack / Akai MPC Live)* | ⚡ **FLAC_AX & AX AV Studio** |
| :--- | :--- | :--- | :--- | :--- |
| **🔊 Audio Signal Path** | Bit-perfect via exclusive ALSA/ASIO/RAAT, but still runs atop a general-purpose OS scheduler | Routed through multi-bus floating-point mixers and plugin wrappers; requires manual latency compensation | Fixed-point/DSP hardware path; external/thru routing typically incurs `~2.0–5.0 ms` converter + buffer latency | **Direct `hw:CARD` ALSA `S32_LE` (`96`-frame period)**; zero mixer, zero resampler, zero software gain staging |
| **🧮 CPU & Thread Determinism** | Subject to OS background tasks, database indexing, and network polling | Subject to GUI thread spikes, GC/plugin allocations, and dynamic core migration | Deterministic embedded ARM/DSP chip, but capped polyphony and fixed FX routing | **Hard `2:3:1` CPU core affinity + `SCHED_FIFO`**; isolated RT audio core with zero syscalls or allocations |
| **💾 Storage & Disk I/O During Playback** | Streams from SSD/NAS with read-ahead buffers | Streams audio clips from disk during playback | Streams long samples from SD/SATA storage | **100% RAM-resident album preloading** (`RamFilePreloader`) on Core `5` prior to playback |
| **🎼 DSD & High-Res PCM Support** | Native/DoP DSD & PCM up to `384 kHz` (paid license/subscription) | PCM only (no native `.dsf`/`.dff` DoP playback) | PCM only (`44.1` / `48 kHz` internal project rate) | **Native PCM (`S32_LE`) + `.dsf`/`.dff` DoP (`DSD64`/`DSD128`)** with `0x05`/`0xFA` marker packing |
| **📈 Visual Telemetry & Latency** | Library cover art & signal-path text readout | Heavy UI compositing competing with audio buffers | Small monochrome/touch LCD at `~30–60 Hz` | **Lock-free double-buffered 256-sample stereo scope + 64-band log FFT** rendered on Core `1` |
| **♻️ Cost & Hardware Lifecycle** | ~£120–£700+ software fees + modern host PC | ~£350–£600 DAW license + audio interface | ~£900–£1,500+ proprietary hardware unit | **Free, open-source C++20**; breathes bare-metal workstation life into preserved `x86_64` CPUs + **£29.67–£35 HiBy FC3 DAC** |

---

## 🏗️ 2. System Architecture & `2:3:1` Topology

All `FLAC_AX` executables share the same deterministic hardware execution model so audio rendering never competes with disk decoding, MIDI polling, or OpenGL visualization:

* **⚡ Core `0` (`0x01` — Real-Time Audio Clock Domain):**
  * Runs the ALSA PCM playback loop (`SCHED_FIFO` Priority `80`) writing interleaved 32-bit signed (`S32_LE`) frames directly to `hw:CARD=FC3,DEV=0` with zero dynamic memory allocations, zero mutex locks, and zero file I/O.
* **🖥️ Core `1` (`0x02` — Video & UI Clock Domain):**
  * `ax_av_studio`: Runs the VBlank-locked SDL2/OpenGL 60 FPS oscilloscope and 64-band logarithmic FFT spectrum visualizer, drains the MIDI queue for HUD telemetry, and flushes rate-limited OLED feedback (`MpkMini3OledFeedback`) every `45 ms`.
  * `flac_ax`: Handles CLI control, terminal status reporting, and signal handling (`SIGINT`/`SIGTERM`).
* **🎛️ Cores `2–4` (`0x1C` — DSP & Synthesis Plane):**
  * Executes polyphonic voice synthesis, resonant SVF filtering, stereo Schroeder reverb, damped tape delay, and `1024`-point Hann FFT analysis.
* **🔌 Core `5` (`0x20` — Peripheral & Preload I/O Plane):**
  * `flac_ax`: Runs `PersistentDecoderWorker`, preloading entire FLAC albums or `.dsf`/`.dff` DSD64/DSD128 files into RAM (`RamFilePreloader`) and feeding the lock-free SPSC ring buffer.
  * `ax_av_studio`: Runs `MpkMini3Listener` (`ALSA RawMIDI` non-blocking reader) parsing incoming notes, pads, pitch bend, and CC messages into a `1,024`-entry power-of-two lock-free ring buffer.

---

## 🛠️ 3. Quick Build & Reference `[AX]` Execution

### 3.1 CMake / Ninja Build (`-j 4` L3 Cache Discipline)

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON \
  -DFLAC_AX_BUILD_VIDEO_UPSCALER=ON \
  -DFLAC_AX_NATIVE_ARCH=OFF \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

### 3.2 Gain-Staged Execution (`[AX]` Reference Rig)

```bash
# Speakers (HiBy FC3 -> Nobsound Q10 -> Wharfedale Diamond 9.0) or Sennheiser HD 560S
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 ~/Music/FLAC_Album/

# Reference Open-Back Headphones (Grado SR325x, 38 Ohm)
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.55 ~/Music/FLAC_Album/

# Live 2.00 ms AV Synthesizer & OpenGL Studio HUD (Akai MPK Mini MK3)
./build/ax_av_studio --fullscreen --device hw:CARD=FC3,DEV=0 --volume 0.80
```
