# 🎛️ FLAC_AX Technical Architecture & Ecosystem Specification

* **Project:** `FLAC_AX` — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio
* **Ecosystem:** The Silicon Preservation Society (`Piccolo [AX]` / `ax-sdk v1_36_1` / `Generation-X-AX` / `piccolo-ax-desktop`)
* **Target Baseline:** Intel Core i5-9600K (6C/6T, HT Disabled), 32 GB DDR4-3600, NVIDIA GTX 980 Ti (6 GB GM200), HiBy FC3 (ESS Sabre `ES9281AC Pro` / `ES9281PRO` — `2.0 Vrms` & `1.3 Vrms` revisions) & Multi-DAC UAC 2.0 Front-Ends + Nobsound Q10 / Wharfedale Diamond 9.0 & Grado SR325x, Akai MPK Mini MK3 (`MPKmini3`) USB-MIDI Controller

---

## ⚙️ 1. System Topology & ESS DAC Clock-Domain Isolation

`FLAC_AX` is engineered as a deterministic, zero-resampling Linux audio and audio-visual runtime where the USB Audio Class 2.0 (UAC 2.0) DAC (defaulting to the HiBy FC3 ESS Sabre `ES9281AC Pro` / `ES9281PRO`) acts as the hardware audio clock source. Desktop mixers (PulseAudio or PipeWire) and software sample-rate conversion are bypassed (`snd_pcm_hw_params_set_rate_resample(0)`), and all streams—PCM, DoP (`DSD64` / `DSD128`), and synchronized video (`AudioMasterPtsSync`)—follow the hardware DAC clock domain.

### 🧠 Physical Core Domain Mapping (`2:3:1` Core Ratio)

On the reference 6-core i5-9600K topology (`coffee_lake_9600k` in `ax-sdk`), `FLAC_AX` enforces strict thread affinity aligned with `ax::topology`:

| Core Mask | Logical Core(s) | Domain Role | `flac_ax` (Audiophile Mode) | `ax_av_studio` (A/V Studio Mode) | `ax_video_upscaler` (Video Upscaler) | `ax_appliance_tui` (Launcher TUI) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`0x01`** | Core 0 | ESS Audio Clock / IRQ | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer + USB xHCI IRQ locked to DAC clock (`S32_LE`, 44.1–384 kHz, DoP 176.4/352.8 kHz) | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer + USB xHCI IRQ (`S32_LE`, 48 kHz, 96-frame period) | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer advancing `AudioMasterPtsSync` from committed DAC frames | Idle (0% CPU; zero interference with Core 0 RT audio) |
| **`0x02`** | Core 1 | Video / HUD | Idle (available for foreground workload) | Pinned VBlank-locked (`SDL_GL_SetSwapInterval(1)`) OpenGL Studio HUD on GTX 980 Ti (60/120 Hz) | Pinned `Core1UpscaleStage` (OpenGL 3.3 Core `Y`/`UV` planar shader, 720p / 1080p / 2k presets, VBlank-paced against `AudioMasterPtsSync`) | Idle (available for foreground workload) |
| **`0x1C`** | Cores 2–4 | Emulation / DSP | Reserved exclusively for foreground emulators (`Eden`, `Cemu`, `Generation-X-AX`) | `FullFeatureSynthEngine` (16 melodic voices + 8 drum voices, 2-pole SVF low-pass, `StereoReverbDsp`, `TapeDelayDsp` & 1024-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads | Idle (isolated `nohz_full` domain untouched) |
| **`0x20`** | Core 5 | Housekeeping / I/O | Pinned `SCHED_OTHER` FLAC/DSD decoder & `RamFilePreloader` (`FLAC::Decoder::File` / `DsdDopStreamer`) | Pinned `SCHED_OTHER` `MpkMini3Listener` (`snd_rawmidi_open` non-blocking reader + off-RT `MpkMini3OledFeedback`) | Pinned `V4l2LiveCaptureSource` (`storage_aux_mask == 0x20ULL` -> Core 5) MMAP capture (YUYV / MJPEG) with lock-free seqlock handoff | Pinned via `sched_setaffinity(Core 5)`; renders 24-bit ANSI Espresso TUI & probes `/run/lock/ax-dac.lock` + `/proc/meminfo` |

### 🔌 End-to-End Signal & Clock-Domain Diagram

```text
+-----------------------------------------------------------------------------+

|                CONTROL & STORAGE DOMAIN — CORE 5 (0x20)                     |
|                                                                             |
|  [Akai MPK Mini MK3] -> [MpkMini3Listener (ALSA RawMIDI)]                   |
|   Keys/Pads, K1-K8 (CC 70-77), Pitch/Mod, Sustain, OLED SysEx               |
|                               |                                             |
|                               v  Lock-Free SPSC Event Ring (1024)          |
|  [NVMe / EROFS toram] -> [RamFilePreloader] -> [FlacRingDecoder / DoP]      |
|  [V4L2 /dev/video0] ----> [V4l2LiveCaptureSource (Seqlock FrameSlot)]       |
|  [Console TTY] ---------> [ax_appliance_tui (Espresso UI & Lock Probe)]     |
+-------------------------------|---------------------------------------------+
                                |
                                v
+-----------------------------------------------------------------------------+

|              DSP & MIXING DOMAIN — CORES 2-4 (0x1C / AVX2+FMA)              |
|                                                                             |
|  [FullFeatureSynthEngine: 16 Melodic + 8 Drum + SVF + Reverb + Delay]       |
|   + 1024-pt Hann FFT Tap                                                    |
|          |                                          |                       |
|          v Lock-Free StereoFrameRing                v Double-Buffered       |
|       (S32_LE PCM / DoP)                          VisualSnapshot          |
+----------|------------------------------------------|-----------------------+

           |                                          |
           v                                          v
+-----------------------------------+   +-------------------------------------+

| ESS DAC CLOCK DOMAIN — CORE 0     |   | VIDEO CLOCK DOMAIN — CORE 1 (0x02)  |
| (0x01 / SCHED_FIFO 80)            |   |                                     |
|                                   |   | [GTX 980 Ti OpenGL 3.3 Core HUD /]  |
| [ALSA Direct hw:CARD=FC3,DEV=0]   |   | [Upscaler (GL_R8 Y + GL_RG8 UV)]    |
|  - S32_LE, 2ch, Rate-Resample = 0 |   |  - VBlank FIFO Lock (SwapInterval 1)|
|  - 96/128-frame + EssHardwareMixer|   |  - 720p / 1080p / 2k + Bob/Cubic    |
|  - AudioMasterPtsSync Master Clock|   |  - AudioMasterPtsSync Frame Pacing  |
|                 |                 |   |                 |                   |
|                 v                 |   |                 v                   |
| [HiBy FC3 / Multi-DAC UAC 2.0]    |   | [HDMI 2.0 / DP 1.2 Display Output]  |
|  - Hardware Audio Clock (UAC 2.0) |   |  - Zero-tearing studio / upscaler   |
|  - Quad DAC / DoP (DSD64 / DSD128)|   |                                     |
+-----------------------------------+   +-------------------------------------+
```

---

## 💾 2. Memory, Storage & USB Bus Engineering

### 🚀 Memory & Storage Topology (`toram` / `tmpfs` / `mlockall`)

* **Process Page Locking (`mlockall(MCL_CURRENT | MCL_FUTURE)`):** Executed at startup in `flac_ax`, `ax_av_studio`, and `ax_video_upscaler` (logging a warning if `CAP_IPC_LOCK` / `RLIMIT_MEMLOCK` is not granted) to eliminate page faults on real-time audio paths.
* **Adaptive RAM Preload Tiers (`DetectHostPreloadPolicy()` in `src/preload_policy.hpp`):**
  * **`WholeAlbum` (`>= 16 GiB` RAM, default on 32 GB rig):** Loads tracks into `RamFilePreloader` up to an `8 GiB` cap (`8ULL * 1024ULL * 1024ULL * 1024ULL`).
  * **`Window3Files` (`6.5 GiB` to `< 16 GiB` RAM):** Maintains a sliding 3-track window in RAM (`2 GiB` cap), evicting completed entries behind `current_index`.
  * **`SingleFile` (`< 6.5 GiB` RAM or `< 2 GiB MemAvailable`):** Restricts preloading to a `512 MiB` cap.
* **Stream Decoding (`FlacRingDecoder` & `DsdDopStreamer`):** `PersistentDecoderWorker` refreshes the preload window on Core 5 (`preloader.RefreshWindow(tracks_, idx)`), inspects `preloader.Find(path)`, and routes `.dsf` / `.dff` tracks through `DsdTrackSession` / `DsdDopStreamer` (`176.4 kHz` DoP for DSD64, `352.8 kHz` DoP for DSD128 via `DopPacker::PackStereoFrame`) or FLAC tracks through `FlacRingDecoder` into `StereoFrameRing` (`262,144` frames).

### 🔌 Physical USB Host Controller Port Separation & Multi-DAC Isolation

* **Dedicated Audio Bus (Core 0 IRQ):** Connect the USB DAC to a dedicated xHCI root port with USB runtime autosuspend disabled (`power/control="on"`, `power/autosuspend="-1"`) via `99-ax-usb-dacs.rules` (covering **HiBy FC3** `0x32BB`/`0x2FC6`, **FiiO KA11** `0x2972`, **TRN Black Pearl** `0x3302`/`0x1A86`, **Moondrop Dawn Pro 2**, and **Apple USB-C UK A2155** `0x05AC`) and PulseAudio/PipeWire ACP claiming suppressed (`PULSE_IGNORE=1`, `ACP_IGNORE=1`).
* **HiBy FC3 Output-Stage Revisions (`2.0 Vrms` vs. `1.3 Vrms`):** Both HiBy FC3 hardware revisions share VID `0x32BB` (`hw:CARD=FC3,DEV=0`). `EssChipProfile` and `ax-mode-cli` default to `--volume 0.80` (`fc3_2v0`) for the `2.0 Vrms` (`112 mW`) revision and support `--volume 0.95` (`fc3_1v3` / `AX_DAC_VOL=0.95` in `/userdata/system/ax-dac.conf`) for the earlier `1.3 Vrms` (`70 mW`) revision when driving TI `TPA3116` / `TPA3255` Class-D speaker amplifiers.
* **Isolated Control / Capture Bus (Core 5):** Keep the Akai MPK Mini MK3 or V4L2 capture device (`V4l2LiveCaptureSource`) on its own port/bus so control and capture I/O do not contend with Core 0 audio submission.

---

## 🧩 3. Internal Subsystems (`src/` & `CMakeLists.txt`)

1. **Build Targets (`CMakeLists.txt`):** Builds `flac_ax` (`src/main.cpp`) plus configurable `ax_av_studio` (`FLAC_AX_BUILD_AV_STUDIO=ON`), `ax_video_upscaler` (`FLAC_AX_BUILD_VIDEO_UPSCALER=ON`), and `ax_appliance_tui` (`FLAC_AX_BUILD_APPLIANCE_TUI=ON`), linking `ax::core` (`ax-sdk v1_36_1` via `find_package(ax-sdk)` with `FetchContent` fallback), `FLAC++` / `FLAC`, `asound`, and optional `SDL2` + `OpenGL` (`FLAC_AX_ENABLE_SDL2_GL` / `FLAC_AX_HAS_SDL2_GL`).
2. **ESS Sabre & Multi-DAC Hardware Hooks, DoP Packer & Mixer (`src/ess_dac_hooks.hpp` & `src/dsd_dop_reader.hpp`):**
