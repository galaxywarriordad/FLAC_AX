# 🏛️ `FLAC_AX` Technical Architecture & Ecosystem Specification

**Project:** `FLAC_AX` — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio  
**Ecosystem:** The Silicon Preservation Society (`Piccolo [AX]` / `ax-sdk` `v1_36_1` / `Generation-X-AX`)  
**Target Baseline:** Intel Core `i5-9600K` (6C/6T, HT Disabled), `32 GB` DDR4-3600, NVIDIA GTX 980 Ti (`6 GB` GM200), **HiBy FC3** (`ESS Sabre ES9281AC Pro` / `ES9281PRO`) USB DAC + **Nobsound Q10** / **Wharfedale Diamond 9.0** & **Grado SR325x**, **Akai MPK Mini MK3** (`MPK mini 3`) USB-MIDI Controller[cite: 39]

---

## 1. System Topology & ESS DAC Clock-Domain Isolation

`FLAC_AX` is engineered as a deterministic, zero-resampling Linux audio and audio-visual runtime where the **HiBy FC3 (`ESS Sabre ES9281AC Pro` / `ES9281PRO`)** serves as the hardware audio clock source over USB Audio Class 2.0 (`UAC 2.0`)[cite: 39, 41]. Desktop mixers (PulseAudio or PipeWire) and software sample-rate conversion are bypassed (`snd_pcm_hw_params_set_rate_resample(0)`), and all streams—PCM, DoP (`DSD64` / `DSD128`), and synchronized video (`AudioMasterPtsSync`)—follow the ESS DAC clock domain[cite: 25, 26, 39, 41, 42, 43, 45].

### Physical Core Domain Mapping (`2:3:1` Core Ratio)

On the reference 6-core `i5-9600K` topology (`coffee_lake_9600k` in `ax-sdk`), `FLAC_AX` enforces strict thread affinity aligned with `ax::topology`:[cite: 39]

| Core Mask | Logical Core(s) | Domain Role | `flac_ax` (Audiophile Mode) | `ax_av_studio` (A/V Studio Mode) | `ax_video_upscaler` (Video Upscaler) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`0x01`** | **Core 0** | `ESS Audio Clock / IRQ` | Pinned `SCHED_FIFO` (`80`) ALSA `snd_pcm_writei` writer + USB xHCI IRQ locked to HiBy FC3 clock (`S32_LE`, `44.1–384 kHz`, DoP `176.4/352.8 kHz`) | Pinned `SCHED_FIFO` (`80`) ALSA `snd_pcm_writei` writer + USB xHCI IRQ (`S32_LE`, `48 kHz`, `96`-frame period) | Pinned `SCHED_FIFO` (`80`) ALSA `snd_pcm_writei` writer advancing `AudioMasterPtsSync` from committed ESS frames |
| **`0x02`** | **Core 1** | `Video / HUD` | Idle (available for foreground workload) | Pinned VBlank-locked (`SDL_GL_SetSwapInterval(1)`) OpenGL Studio HUD on GTX 980 Ti (`60/120 Hz`) | Pinned `Core1UpscaleStage` (`720p` / `1080p` / `2k` presets, VBlank-paced against ESS `AudioMasterPtsSync`) |
| **`0x1C`** | **Cores 2–4** | `Emulation / DSP` | Reserved exclusively for foreground emulators (`Eden`, `Cemu`, `Generation-X-AX`) | `FullFeatureSynthEngine` (16 melodic voices + 8 drum voices, 2-pole SVF low-pass, `StereoReverbDsp`, `TapeDelayDsp` & `1024`-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads |
| **`0x20`** | **Core 5** | `Housekeeping / I/O` | Pinned `SCHED_OTHER` FLAC/DSD decoder & `RamFilePreloader` (`FLAC::Decoder::File` / DoP stream) | Pinned `SCHED_OTHER` `MpkMini3Listener` (`snd_rawmidi_open` non-blocking reader + off-RT `MpkMini3OledFeedback`) | Pinned `V4l2LiveCaptureSource` (`storage_aux_mask == 0x20ULL` -> Core `5`) `MMAP` capture (`YUYV` / `MJPEG`)[cite: 25, 26, 30, 31, 33, 36, 39, 42, 43, 45, 46, 47]

### End-to-End Signal & Clock-Domain Diagram

```text
+---------------------------------------------------------------------------------------------------+
|                        CONTROL & STORAGE DOMAIN — CORE 5 (0x20 / SCHED_OTHER)                     |
|                                                                                                   |
|  [Akai MPK Mini MK3] --(USB 1.1 FS)--> [MpkMini3Listener (ALSA RawMIDI)]                         |
|   25 Keys | 8 Pads | K1-K8 (CC 70-77) | Pitch/Mod (CC 1) | Sustain (CC 64) | OLED SysEx Hook       |
|                                                           |                                       |
|                                                           v  Lock-Free SPSC Event Ring (1024)     |
|  [NVMe / EROFS toram] --> [RamFilePreloader] ---> [FlacRingDecoder / DsdDopStreamer]              |
|  [V4L2 /dev/video0] -----> [V4l2LiveCaptureSource (MMAP YUYV/MJPEG)]                             |
+-----------------------------------------------------------|---------------------------------------+
                                                            |
                                                            v
+---------------------------------------------------------------------------------------------------+
|                        DSP & MIXING DOMAIN — CORES 2–4 (0x1C / nohz_full / AVX2+FMA)              |
|                                                                                                   |
|  [FullFeatureSynthEngine: 16 Melodic + 8 Drum + SVF + Reverb + Delay] + [1024-pt Hann FFT Tap] |
|          |                                                              |                         |
|          | Lock-Free StereoFrameRing                                    | Double-Buffered         |
|          | (S32_LE PCM / DoP)                                           | VisualSnapshot          |
+----------|--------------------------------------------------------------|-------------------------+
           |                                                              |
           v                                                              v
+----------------------------------------------------+  +-------------------------------------------+
|   ESS DAC CLOCK DOMAIN — CORE 0 (0x01 / FIFO 80)   |  |  VIDEO CLOCK DOMAIN — CORE 1 (0x02)       |
|                                                    |  |                                           |
|  [ALSA Direct hw:CARD=FC3,DEV=0]                   |  |  [GTX 980 Ti (GM200) OpenGL HUD / Upscaler] |
|   - S32_LE Interleaved, 2ch, Rate-Resample = 0     |  |   - VBlank FIFO Lock (SDL_GL_SetSwapInterval(1)) |
|   - 96/128-frame period + EssHardwareMixer         |  |   - 720p / 1080p / 2k + BT.601/Bob/Cubic  |
|   - AudioMasterPtsSync (DVD / LiveLowLatency)      |  |   - AudioMasterPtsSync Frame Pacing       |
|                    |                               |  |                    |                      |
|                    v                               |  |                    v                      |
|  [HiBy FC3 / ESS Sabre ES9281AC Pro]               |  |  [HDMI 2.0 / DP 1.2 Display Output]       |
|   - Hardware Audio Clock Source (UAC 2.0)          |  |   - Zero-tearing studio / upscaler output |
|   - 32-Bit HyperStream II Quad DAC / DoP (DSD64/128)|  |                                           |
+----------------------------------------------------+  +-------------------------------------------+
```[cite: 25, 26, 31, 33, 36, 39, 41, 42, 43, 45, 46, 47, 48]

---

## 2. Memory, Storage & USB Bus Engineering

### Memory & Storage Topology (`toram` / `tmpfs` / `mlockall`)

* **Process Page Locking (`mlockall(MCL_CURRENT | MCL_FUTURE)`):** Attempted at startup in `flac_ax`, `ax_av_studio`, and `ax_video_upscaler` (logging a warning if `CAP_IPC_LOCK` / `RLIMIT_MEMLOCK` is not granted) to reduce page faults on real-time audio paths
* **Adaptive RAM Preload Tiers (`DetectHostPreloadPolicy()` in `src/preload_policy.hpp`):**
  * **`WholeAlbum` (`>= 16 GiB` RAM, default on 32 GB rig):** Loads tracks into `RamFilePreloader` up to an `8 GiB` cap (`8ULL * 1024ULL * 1024ULL * 1024ULL`)
  * **`Window3Files` (`6.5 GiB` to `< 16 GiB` RAM):** Maintains a sliding 3-track window in RAM (`2 GiB` cap), evicting completed entries behind `current_index`
  * **`SingleFile` (`< 6.5 GiB` RAM or `< 2 GiB` `MemAvailable`):** Restricts preloading to a `512 MiB` cap
* **Stream Decoding (`FlacRingDecoder` & `DsdDopStreamer`):** Decodes FLAC tracks via `FLAC::Decoder::File` into the stereo `S32_LE` ring buffer and streams `.dsf` / `.dff` (`DSD64` at `176.4 kHz` DoP, `DSD128` at `352.8 kHz` DoP) via `DsdDopStreamer` (note: `main.cpp` currently references `flac_ax::preload::RamFilePreloader` / `PreloadedEntry` / `TryLoad` and `DopPacker::PackStereoFrame`, which need alignment with `preload_policy.hpp` and `ess_dac_hooks.hpp`)[cite: 25, 26, 30, 39, 41, 42, 43]

### Physical USB Host Controller Port Separation

* **Dedicated Audio Bus (`Core 0` IRQ):** Connect the **HiBy FC3** (`ES9281AC Pro` / `ES9281PRO`) to a dedicated xHCI root port with USB runtime autosuspend disabled (`power/control=on` for HiBy `VID 0x32BB`)[cite: 39, 41]
* **Isolated Control / Capture Bus (`Core 5`):** Keep the **Akai MPK Mini MK3** or V4L2 capture device (`V4l2LiveCaptureSource`) on its own port/bus so control and capture I/O do not contend with audio submission[cite: 31, 39, 46, 47]

---

## 3. Internal Subsystems (`src/` & `CMakeLists.txt`)

1. **Build Targets (`CMakeLists.txt`):** Builds three executables when `FLAC_AX_BUILD_LINUX_APPLIANCE` is enabled: `flac_ax` (`src/main.cpp`), `ax_av_studio` (`src/studio_main.cpp`), and `ax_video_upscaler` (`src/video_upscaler_main.cpp`), linking `ax::core` (`ax-sdk` `v1_36_1` via `find_package(ax-sdk)` with `FetchContent` fallback), `FLAC++` / `FLAC`, `asound`, and optional `SDL2` + `OpenGL` (`FLAC_AX_HAS_SDL2_GL`)[cite: 16, 25, 26, 43].
2. **ESS Sabre Hardware Hooks, DoP Packer & Mixer (`src/ess_dac_hooks.hpp` & `src/dsd_dop_reader.hpp`):**
   * Resolves `ES9281AC Pro` and `ES9281PRO` profiles (`MakeEssProfile`, `ResolveEssChipHook`), recommended load volumes (`RecommendedVolumeForTarget`), sample rate negotiation (`NegotiateEssRate` within `[44100 Hz, 384000 Hz]`), and buffer period counts (`RecommendedBufferPeriods`: `8` at `>= 176.4 kHz`, `4` otherwise)[cite: 39, 41].
   * Provides `DopPacker` (`0x05` / `0xFA` markers) and `ParseDsdMemoryHeader` / `ParseDsdFileHeader` / `DsdDopStreamer` for `.dsf` and `.dff` streams[cite: 41, 42].
   * Provides `EssHardwareMixer` (`--volume` / `--mixer-card`) to control ALSA playback volume on the target card (`FC3` default)[cite: 25, 26, 41, 43].
3. **Akai MPK Mini MK3 ALSA RawMIDI & OLED Feedback (`src/midi/mpk_mini3.hpp`, `.cpp` & `src/midi/mpk_mini3_oled.hpp`):**
   * Auto-discovers `MPKmini3` in `/proc/asound/cards` (`hw:CARD=MPKmini3,DEV=0`), reads non-blocking ALSA RawMIDI on Core `5`, parses running status + SysEx framing (`0xF0..0xF7`), and pushes events into a 1,024-entry lock-free SPSC queue[cite: 31, 32, 39].
   * `MpkMini3OledFeedback` rate-limits (`45 ms` default interval) off-RT parameter/SysEx feedback for `K1–K8` (`CC 70–77`)[cite: 33].
4. **Synthesizer & Effects DSP (`src/dsp/full_feature_synth.hpp`, `src/dsp/stereo_reverb_dsp.hpp`, `src/dsp/tape_delay_dsp.hpp` & `src/dsp/avx2_synth.hpp`/`.cpp`):**
   * `FullFeatureSynthEngine` (used by `studio_main.cpp`) runs 16 melodic voices + 8 drum voices, a 2-pole SVF low-pass filter + soft saturation, `StereoReverbDsp`, `TapeDelayDsp`, and a `1024`-point Hann-windowed radix-2 FFT + `256`-sample stereo scope into double-buffered `VisualSnapshot` slots[cite: 25, 34, 35, 36].
   * `PolySynthEngine` (`src/dsp/avx2_synth.hpp`/`.cpp`) provides the standalone 16-voice saw+sub + 2-pole SVF + `1024`-pt FFT engine[cite: 37, 38].
5. **720p/1080p/2K Video Upscaler & Audio-Master Sync (`src/video_upscaler_main.cpp` & `src/video/*.hpp`):**
   * Resolves hardware guardrails via `Resolve720pGuardrails` (`5.50` GiB VRAM ceiling, `5.00` GiB reap trigger, `0x20` storage/aux mask)[cite: 43, 49].
   * Captures live retro video on Core `5` via `V4l2LiveCaptureSource` (`YUYV` / `MJPEG` `MMAP`) or paces DVD streams, paces frames via `AudioMasterPtsSync` (`AudioMasterDvd` / `LiveLowLatency`) against the ESS audio clock, and runs `Core1UpscaleStage` on Core `1` with `kUpscale720pFragmentShader` (`240p`, `bob`, `dvd` scan modes)[cite: 43, 45, 46, 47, 48].
6. **Shared DAC Arbitration (`src/studio_main.cpp`, `src/main.cpp`, `src/video_upscaler_main.cpp`):**
   * Coordinates exclusive access to `hw:CARD=FC3,DEV=0` via `/run/lock/ax-dac.lock` (`flock(LOCK_EX | LOCK_NB)`) and `SIGINT`/`SIGTERM`/`SIGUSR1` handlers[cite: 25, 26, 39, 43].

---

## 4. Wider Silicon Preservation Society Repo Topology

`FLAC_AX` integrates with `ax-sdk` (`v1_36_1`), `Piccolo-AX` (`batocera-ax-external`), `piccolo-ax-desktop`, `Generation-X-AX`, `Cemu-Latte-AX`, and `Eden-Espresso-Wave-AX` across the Silicon Preservation Society ecosystem, sharing `ax-sdk` profiles/core topology and `/run/lock/ax-dac.lock` DAC arbitration[cite: 25, 26, 39, 43].

---

## 5. Current Status & Next Steps

### Delivered in Current Tree

* Core audio (`src/main.cpp`), studio synth/MIDI/OLED runner (`src/studio_main.cpp`), and 720p/1080p/2K video upscaler runner (`src/video_upscaler_main.cpp`) wired in `CMakeLists.txt`
* `src/ess_dac_hooks.hpp`, `src/dsd_dop_reader.hpp`, `src/preload_policy.hpp`, `src/midi/mpk_mini3.*`, `src/midi/mpk_mini3_oled.hpp`, `src/dsp/full_feature_synth.hpp`, `src/dsp/stereo_reverb_dsp.hpp`, `src/dsp/tape_delay_dsp.hpp`, `src/dsp/avx2_synth.*`, and `src/video/*.hpp`[cite: 16, 25, 26, 30, 31, 32, 33, 34, 35, 36, 37, 38, 41, 42, 43, 45, 47, 48, 49]

### Remaining Fixes & Milestones

1. **Reconcile `main.cpp`, `preload_policy.hpp`, `ess_dac_hooks.hpp`, and `dsd_dop_reader.hpp`:** Align `RamFilePreloader` constructor/API usage in `main.cpp` with `preload_policy.hpp` and add `DopPacker::PackStereoFrame` (or call `PackStereoInterleaved`) so `flac_ax` compiles cleanly.
2. **Complete `Core1UpscaleStage` shader/texture upload:** Compile `kUpscale720pFragmentShader` and bind `CapturedFrame` Y/UV textures in `Core1UpscaleStage::RenderFrame` instead of clearing to a solid color[cite: 43, 46, 48].
3. **Package in `Piccolo-AX` (`batocera-ax-external`):** Wire `flac_ax`, `ax_av_studio`, and `ax_video_upscaler` into Buildroot and deploy udev rules for `VID 0x32BB`[cite: 39].
