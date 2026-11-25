# 🎛️ FLAC\_AX & AX AV Studio — Complete User Guide & Build Manual

---

## 🚀 1\. Introduction: Why FLAC\_AX Exists & How It Stacks Up

Modern desktop operating systems treat audio as a background convenience service. Even on purpose-built workstations, general-purpose audio stacks route sound through software mixers, sample-rate converters, dynamic thread schedulers, and power-managed USB hubs—introducing jitter, buffer underruns, and silent bit-depth truncation. Meanwhile, commercial audiophile players and standalone hardware synthesizers force a binary choice: either pay ongoing subscription fees for heavy desktop apps tethered to bloated operating systems, or spend thousands of pounds on dedicated proprietary DSP boxes with closed firmware and modest embedded CPUs.

**FLAC\_AX** (`flac_ax` \+ `ax_av_studio` \+ `ax_video_upscaler` \+ `ax_appliance_tui` \+ `ax_clap_host` \+ `ax_lv2_host`) rejects both compromises. Built in zero-dependency C++20 around a deterministic **`2:3:1` physical CPU core topology** on `Linux 6.12-rt` (`Piccolo [AX]`) and standard `x86_64` Linux workstations (plus cross-platform Windows builds), it turns proven, off-the-shelf `x86_64` silicon (such as an Intel Core `i5-9600K` or `i5-8600K`) paired with an **ESS Sabre USB DAC** (**HiBy FC3** / `ES9281AC Pro` or `ES9281PRO`), a **Nobsound Q10 \+ Wharfedale Diamond 9.0** desktop speaker rig or **Grado SR325x** open-back headphones, and an **Akai MPK Mini MK3** into a deterministic, bare-metal audiophile transport and `2.00 ms` live synthesizer & plugin host workstation.

### ✨ What Makes FLAC\_AX Special

1. **⚡ Deterministic `2:3:1` Core Partitioning (Zero Scheduler Contention)** Instead of letting the OS kernel migrate audio threads across cores alongside UI rendering and disk I/O, `FLAC_AX` pins workloads strictly by role:  
   * **Core `0` (Real-Time Audio Clock Domain):** `SCHED_FIFO` (Priority `80`) ALSA `S32_LE` / `DoP` output directly to `hw:CARD=FC3,DEV=0` (and real sink drain threads in `ax_clap_host` and `ax_lv2_host`), USB xHCI IRQ handling, and `AudioMasterPtsSync` master clock—completely isolated from disk reads, heap allocations, and mutexes.  
   * **Core `1` (Video & HUD Clock Domain):** VBlank-locked SDL2/OpenGL oscilloscope and 64-band FFT spectrum visualizer (`ax_av_studio`), off-RT MIDI OLED SysEx feedback, and `ax_video_upscaler` OpenGL 3.3 Core Y/UV frame presentation.  
   * **Cores `2–4` (DSP, Plugin Host & Synthesis Plane):** CLAP and LV2 audio plugin hosting (`ax_clap_host` and `ax_lv2_host` pinned to `0x1C`), 16-voice polyphonic saw/sub synthesizer, 8-voice percussion engine, 2-pole stereo State-Variable Filter, 4-comb `StereoReverbDsp`, and interpolated `TapeDelayDsp`.  
   * **Core `5` (Peripheral & Preload I/O Plane):** Adaptive RAM preloading (`RamFilePreloader`), FLAC/DSD/OpenMPT tracker decoding, lock-free ALSA RawMIDI ingestion (`MpkMini3Listener`), MS2130S V4L2 capture, and `ax_appliance_tui`.  
2. **🧠 True RAM-Resident, Zero-Allocation Playback & Synthesis**  
   * **`flac_ax` (Bit-Perfect Transport):** Preloads FLAC albums, native `.dsf`/`.dff` DSD64/DSD128 streams, and tracker modules (`.mod`/`.xm`/`.s3m`/`.it` via `libopenmpt`) into RAM (`mlockall`) on Core `5` before streaming raw `S32_LE` or `DoP` frames directly to `hw:CARD=FC3,DEV=0`—eliminating storage bus interrupts, PipeWire/PulseAudio resampling, and software volume scaling.  
   * **`ax_av_studio` (Live AV Synthesizer):** Runs a 16-voice polyphonic melodic synth, 8-voice drum engine, 2-pole stereo State-Variable Filter, 4-comb `StereoReverbDsp`, and interpolated `TapeDelayDsp` entirely over preallocated `std::array` buffers with zero heap allocations on the real-time thread.  
   * **`ax_clap_host` & `ax_lv2_host` (Low-Latency Plugin Hosts):** Load CLAP (`clap_entry`, C ABI) and LV2 (`lilv`, URID/Atom) plugins on isolated DSP cores (`0x1C`) and feed a canonical `StereoFloatRing` drained by a Core `0` (`SCHED_FIFO 80`) `AlsaStereoSink` (`S32_LE`, with `--device` / `--no-alsa` support and optional `ax-sdk` telemetry metrics).  
3. **🔒 Hardware-Level DAC Arbitration, Canonical Ring Transport & USB Power Tuning**  
   * All `FLAC_AX` audio targets share a single cache-line-aligned `StereoSampleRing<SampleT>` (`StereoFrameRing32` for `int32_t` and `StereoFloatRing` for `float` with `FloatSampleToS32` / `ConvertStereoFloatToS32` helpers) in `src/audio/ax_stereo_ring.hpp`.  
   * Exclusive ownership of the ESS Sabre DAC is coordinated via `/run/lock/ax-dac.lock` (`ScopedDacLock` in `src/audio/ax_dac_lock.hpp`, using `flock` on the open descriptor and closing/unlocking cleanly without unlinking the shared lock path on exit) while disabling USB autosuspend for `VID 0x32BB` (`power/control="on"`).

---

### 📊 How FLAC\_AX Compares to Commercial Software & Hardware

| Dimension | 💿 Commercial Audiophile Players *(Roon / Audirvana Studio)* | 🎛️ Commercial Desktop DAWs *(Ableton Live / Bitwig)* | 🎛️ Standalone Hardware Boxes *(Elektron Octatrack / Akai MPC Live)* | ⚡ FLAC\_AX & AX AV Studio |
| :---- | :---- | :---- | :---- | :---- |
| **🔊 Audio Signal Path** | Bit-perfect via exclusive ALSA/ASIO/RAAT, but still runs atop a general-purpose OS scheduler | Routed through multi-bus floating-point mixers and plugin wrappers; requires manual latency compensation | Fixed-point/DSP hardware path; external/thru routing typically incurs `~2.0–5.0 ms` converter \+ buffer latency | **Direct `hw:CARD` ALSA `S32_LE` (`96`\-frame period) \+ CLAP/LV2 hosts**; zero mixer, zero resampler, zero software gain staging |
| **🧠 CPU & Thread Determinism** | Subject to OS background tasks, database indexing, and network polling | Subject to GUI thread spikes, GC/plugin allocations, and dynamic core migration | Deterministic embedded ARM/DSP chip, but capped polyphony and fixed FX routing | **Hard `2:3:1` CPU core affinity \+ `SCHED_FIFO`**; isolated RT audio core with zero syscalls or allocations |
| **💾 Storage & Disk I/O During Playback** | Streams from SSD/NAS with read-ahead buffers | Streams audio clips from disk during playback | Streams long samples from SD/SATA storage | **Adaptive RAM-resident album preloading** (`RamFilePreloader`) on Core `5` prior to playback |
| **🎵 DSD, Tracker & High-Res PCM Support** | Native/DoP DSD & PCM up to `384 kHz` (paid license/subscription) | PCM only (no native `.dsf`/`.dff` DoP playback) | PCM only (`44.1` / `48 kHz` internal project rate) | **Native PCM (`S32_LE`) \+ `.dsf`/`.dff` DoP (`DSD64`/`DSD128`) \+ `.mod`/`.xm`/`.s3m`/`.it` tracker modules** |
| **📈 Visual Telemetry & Latency** | Library cover art & signal-path text readout | Heavy UI compositing competing with audio buffers | Small monochrome/touch LCD at `~30–60 Hz` | **Lock-free double-buffered 256-sample stereo scope \+ 64-band log FFT** rendered on Core `1` |
| **♻️ Cost & Hardware Lifecycle** | `~£120–£700+` software fees \+ modern host PC | `~£350–£600` DAW license \+ audio interface | `~£900–£1,500+` proprietary hardware unit | **Free, open-source C++20**; breathes bare-metal workstation life into preserved `x86_64` CPUs \+ **£29.67–£35 HiBy FC3 DAC** |

---

## 🏗️ 2\. System Architecture, `2:3:1` Topology & Shared Audio Layer

All `FLAC_AX` executables share the same deterministic hardware execution model so audio rendering never competes with disk decoding, MIDI polling, or OpenGL visualization:

* **⚡ Core `0` (`0x01` — Real-Time Audio Clock Domain):**  
  * Runs the ALSA PCM playback loop (`SCHED_FIFO` Priority `80`) writing interleaved 32-bit signed (`S32_LE`) frames directly to `hw:CARD=FC3,DEV=0` with zero dynamic memory allocations, zero mutex locks, and zero file I/O.  
  * Drains `StereoFloatRing` via `AlsaStereoSink` (`S32_LE`, `--device` / `--no-alsa` support, with timing fallback when ALSA is unavailable or disabled) in `ax_clap_host` and `ax_lv2_host`.  
* **🖥️ Core `1` (`0x02` — Video & UI Clock Domain):**  
  * `ax_av_studio`: Runs the VBlank-locked SDL2/OpenGL 60 FPS oscilloscope and 64-band logarithmic FFT spectrum visualizer, drains the MIDI queue for HUD telemetry, and flushes rate-limited OLED feedback (`MpkMini3OledFeedback`) every `45 ms`.  
  * `ax_video_upscaler`: Runs OpenGL 3.3 Core Y/UV planar shaders (`GL_R8` \+ `GL_RG8`) and `AudioMasterPtsSync` pacing for 720p/1080p/2K DVD and retro HDMI capture.  
  * `flac_ax`: Handles CLI control, terminal status reporting, and signal handling (`SIGINT`/`SIGTERM`/`SIGUSR1`).  
* **🎛️ Cores `2–4` (`0x1C` — DSP, Plugin Host & Synthesis Plane):**  
  * Hosts CLAP (`ax_clap_host`) and LV2 (`ax_lv2_host`) plugins on `0x1C`, pushing stereo float blocks into `StereoFloatRing`.  
  * Executes polyphonic voice synthesis, resonant SVF filtering, stereo Schroeder reverb, damped tape delay, and `1024`\-point Hann FFT analysis.  
* **🔌 Core `5` (`0x20` — Peripheral & Preload I/O Plane):**  
  * `flac_ax`: Runs `RamFilePreloader` and FLAC / DoP DSD / `libopenmpt` tracker decoding into `StereoFrameRing32`.  
  * `ax_av_studio`: Runs `MpkMini3Listener` (`ALSA RawMIDI` non-blocking reader) parsing incoming notes, pads, pitch bend, and CC messages into a `1,024`\-entry power-of-two lock-free ring buffer (with `[[maybe_unused]]` padding members in `src/midi/mpk_mini3.hpp` for warning-free Clang builds).  
  * `ax_appliance_tui` & `v4l2_live_capture`: Runs the Core `5` Coffee/Espresso appliance TUI and zero-lag `MS2130S` V4L2 capture worker.

---

## 🛠️ 3\. Quick Build & Reference `[AX]` Execution

### 3.1 CMake / Ninja Build (`-j 4` L3 Cache Discipline)

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON \
  -DFLAC_AX_BUILD_VIDEO_UPSCALER=ON \
  -DFLAC_AX_BUILD_APPLIANCE_TUI=ON \
  -DFLAC_AX_BUILD_LV2_HOST=ON \
  -DFLAC_AX_BUILD_CLAP_HOST=ON \
  -DFLAC_AX_ENABLE_SDL2_GL=ON \
  -DFLAC_AX_SKYLAKE_ARCH=ON \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

### 3.2 Gain-Staged & Plugin Host Execution (`[AX]` Reference Rig)

```sh
# Speakers — 2.0 Vrms HiBy FC3 (112 mW) -> Nobsound Q10 -> Wharfedale Diamond 9.0
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 ~/Music/FLAC_Album/*.flac

# Speakers — Early 1.3 Vrms HiBy FC3 Revision (70 mW) -> Nobsound Q10 / Class-D Amp
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.95 ~/Music/FLAC_Album/*.flac

# Speakers — TRN Black Pearl / Moondrop Dawn Pro 2 (2.0 Vrms) -> Class-D Amp
./build/flac_ax --device hw:CARD=BLACKPEARL,DEV=0 --mixer-card BLACKPEARL --volume 0.80 ~/Music/FLAC_Album/*.flac

# Speakers — 2.5 Vrms FiiO KA11 (capped to prevent Class-D input clipping)
./build/flac_ax --device hw:CARD=KA11,DEV=0 --mixer-card KA11 --volume 0.72 ~/Music/FLAC_Album/*.flac

# Reference Open-Back Headphones (Grado SR325x, 38 Ohm)
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.55 ~/Music/FLAC_Album/*.flac

# Live 2.00 ms AV Synthesizer & OpenGL Studio HUD (Akai MPK Mini MK3)
./build/ax_av_studio --fullscreen --device hw:CARD=FC3,DEV=0 --volume 0.80

# Single-Stream 1080p Retro HDMI Capture Upscaler (MacroSilicon MS2130S -> Core 1 GL Shader)
./build/ax_video_upscaler --mode retro --preset 1080p --v4l2 /dev/video0 --scan bob --device hw:CARD=FC3,DEV=0 --volume 0.80

# CLAP Plugin Host (Cores 2-4 DSP -> StereoFloatRing -> Core 0 ALSA S32_LE Sink)
./build/ax_clap_host /path/to/plugin.clap "" 48000 96 --device hw:CARD=FC3,DEV=0

# LV2 Plugin Host (Cores 2-4 DSP -> StereoFloatRing -> Core 0 ALSA S32_LE Sink)
./build/ax_lv2_host "http://example.org/lv2/plugin" 48000 96 --device hw:CARD=FC3,DEV=0

# Core 5 Coffee/Espresso Appliance TUI Launcher
./build/ax_appliance_tui
```

