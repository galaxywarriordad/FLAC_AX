# FLAC\_AX Technical Architecture & Ecosystem Specification

* **Project:** `FLAC_AX` — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio  
* **Ecosystem:** The Silicon Preservation Society (`Piccolo [AX]` / `ax-sdk v1_36_2` / `Generation-X-AX` / `piccolo-ax-desktop`)  
* **Target Baseline:** Intel Core i5-9600K (6C/6T, HT Disabled), 32 GB DDR4-3600, NVIDIA GTX 980 Ti (6 GB GM200), HiBy FC3 (ESS Sabre `ES9281AC Pro` / `ES9281PRO` — `2.0 Vrms` & `1.3 Vrms` revisions) & Multi-DAC UAC 2.0 Front-Ends \+ Nobsound Q10 / Wharfedale Diamond 9.0 & Grado SR325x, Akai MPK Mini MK3 (`MPKmini3`) USB-MIDI Controller

---

## Deterministic Memory Governance & Read-Only `ax_doctor` Role

* **Explicit Memory Budgets Over AI/ML Guessing:** Memory tiering (`WholeAlbum` `8 GiB` cap on `>= 16 GiB` RAM, `Window3Files` `2 GiB` cap on `6.5 GiB` to `< 16 GiB` RAM, `SingleFile` `512 MiB` cap or `64 MiB` on `<= 1.5 GiB` handhelds), `mlockall(MCL_CURRENT | MCL_FUTURE)`, `toram` `/dev/shm` `EROFS` (`< 512 MB`), and `VkVramShield` ceilings (`5.50 GiB` ceiling / `5.00 GiB` reap trigger) use deterministic rules and fixed thresholds rather than dynamic AI/ML guessing or background inference daemons.  
* **Read-Only `ax_doctor` Pre-Flight Role:** `ax_doctor` serves strictly as an on-demand, read-only diagnostic and pre-flight check verifying kernel cmdline isolation flags, `MemAvailable` and HugeTLB/zram/toram headroom, IRQ affinity masks, and `/run/lock/ax-dac.lock` / DAC readiness without background polling or silent mutation.  
* **Offline Dev/CI Separation:** Any AI or log-parsing tooling stays strictly offline on CPU/RAM outside the live appliance image and outside GPU VRAM.

---

## 1\. System Topology & ESS DAC Clock-Domain Isolation

`FLAC_AX` is engineered as a deterministic, zero-resampling Linux audio and audio-visual runtime where the USB Audio Class 2.0 (UAC 2.0) DAC (defaulting to the HiBy FC3 ESS Sabre `ES9281AC Pro` / `ES9281PRO`) acts as the hardware audio clock source. Desktop mixers (PulseAudio or PipeWire) and software sample-rate conversion are bypassed (`snd_pcm_hw_params_set_rate_resample(0)`), and all streams—PCM, DoP (`DSD64` / `DSD128`), and synchronized video (`AudioMasterPtsSync`)—follow the hardware DAC clock domain.

### Physical Core Domain Mapping (`2:3:1` Core Ratio)

On the reference 6-core i5-9600K topology (`coffee_lake_9600k` in `ax-sdk`), `FLAC_AX` enforces strict thread affinity aligned with `ax::topology`:

| Core Mask | Logical Core(s) | Domain Role | `flac_ax` (Audiophile Mode) | `ax_av_studio` (A/V Studio Mode) | `ax_video_upscaler` (Video Upscaler) | `ax_appliance_tui` (Launcher TUI) |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| **`0x01`** | Core 0 | ESS Audio Clock / IRQ | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer \+ USB xHCI IRQ locked to DAC clock (`S32_LE`, 44.1–384 kHz, DoP 176.4/352.8 kHz) | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer \+ USB xHCI IRQ (`S32_LE`, 48 kHz, 96-frame period) | Pinned `SCHED_FIFO` (80) ALSA `snd_pcm_writei` writer advancing `AudioMasterPtsSync` from committed DAC frames | Idle (0% CPU; zero interference with Core 0 RT audio) |
| **`0x02`** | Core 1 | Video / HUD | Idle (available for foreground workload) | Pinned VBlank-locked (`SDL_GL_SetSwapInterval(1)`) OpenGL Studio HUD on GTX 980 Ti (60/120 Hz) | Pinned `Core1UpscaleStage` (OpenGL 3.3 Core `Y`/`UV` planar shader, 720p / 1080p / 2k presets, VBlank-paced against `AudioMasterPtsSync`) | Idle (available for foreground workload) |
| **`0x1C`** | Cores 2–4 | Emulation / DSP | Reserved exclusively for foreground emulators (`Eden`, `Cemu`, `Generation-X-AX`) | `FullFeatureSynthEngine` (16 melodic voices \+ 8 drum voices, 2-pole SVF low-pass, `StereoReverbDsp`, `TapeDelayDsp` & 1024-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads | Idle (isolated `nohz_full` domain untouched) |
| **`0x20`** | Core 5 | Housekeeping / I/O | Pinned `SCHED_OTHER` FLAC/DSD decoder & `RamFilePreloader` (`FLAC::Decoder::File` / `DsdDopStreamer`) | Pinned `SCHED_OTHER` `MpkMini3Listener` (`snd_rawmidi_open` non-blocking reader \+ off-RT `MpkMini3OledFeedback`) | Pinned `V4l2LiveCaptureSource` (`storage_aux_mask == 0x20ULL` \-\> Core 5\) MMAP capture (YUYV / MJPEG) with lock-free seqlock handoff | Pinned via `sched_setaffinity(Core 5)`; renders 24-bit ANSI Espresso TUI & probes `/run/lock/ax-dac.lock` \+ `/proc/meminfo` |

### End-to-End Signal & Clock-Domain Diagram

```
+-----------------------------------------------------------------------------+
| CONTROL & STORAGE DOMAIN — CORE 5 (0x20)                                    |
|                                                                             |
| [Akai MPK Mini MK3] -> [MpkMini3Listener (ALSA RawMIDI)]                    |
| Keys/Pads, K1-K8 (CC 70-77), Pitch/Mod, Sustain, OLED SysEx                |
|        |                                                                    |
|        v Lock-Free SPSC Event Ring (1024)                                   |
| [NVMe / EROFS toram] -> [RamFilePreloader] -> [FlacRingDecoder / DoP]        |
| [V4L2 /dev/video0] ----> [V4l2LiveCaptureSource (Seqlock FrameSlot)]        |
| [Console TTY] ---------> [ax_appliance_tui (Espresso UI & Lock Probe)]      |
+-------------------------------|---------------------------------------------+
                                |
                                v
+-----------------------------------------------------------------------------+
| DSP & MIXING DOMAIN — CORES 2-4 (0x1C / AVX2+FMA)                           |
|                                                                             |
| [FullFeatureSynthEngine: 16 Melodic + 8 Drum + SVF + Reverb + Delay]       |
| + 1024-pt Hann FFT Tap                                                      |
|        |                                       |                            |
|        v Lock-Free StereoFrameRing              v Double-Buffered             |
| (S32_LE PCM / DoP)                            VisualSnapshot                |
+----------|------------------------------------------|-----------------------+
           |                                          |
           v                                          v
+-----------------------------------+ +-------------------------------------+
| ESS DAC CLOCK DOMAIN — CORE 0     | | VIDEO CLOCK DOMAIN — CORE 1 (0x02)  |
| (0x01 / SCHED_FIFO 80)            | |                                     |
|                                   | | [GTX 980 Ti OpenGL 3.3 Core HUD /]  |
| [ALSA Direct hw:CARD=FC3,DEV=0]   | | [Upscaler (GL_R8 Y + GL_RG8 UV)]    |
| - S32_LE, 2ch, Rate-Resample = 0  | | - VBlank FIFO Lock (SwapInterval 1) |
| - 96/128-frame + EssHardwareMixer | | - 720p / 1080p / 2k + Bob/Cubic   |
| - AudioMasterPtsSync Master Clock | | - AudioMasterPtsSync Frame Pacing   |
|        |                          | |        |                            |
|        v                          | |        v                            |
| [HiBy FC3 / Multi-DAC UAC 2.0]    | | [HDMI 2.0 / DP 1.2 Display Output]  |
| - Hardware Audio Clock (UAC 2.0)  | | - Zero-tearing studio / upscaler    |
| - Quad DAC / DoP (DSD64 / DSD128) | |                                     |
+-----------------------------------+ +-------------------------------------+
```

---

## 2\. Memory, Storage & USB Bus Engineering

### Memory & Storage Topology (`toram` / `tmpfs` / `mlockall`)

* **Process Page Locking (`mlockall(MCL_CURRENT | MCL_FUTURE)`):** Executed at startup in `flac_ax`, `ax_av_studio`, and `ax_video_upscaler` (logging a warning if `CAP_IPC_LOCK` / `RLIMIT_MEMLOCK` is not granted) to eliminate page faults on real-time audio paths.  
* **Adaptive RAM Preload Tiers (`DetectHostPreloadPolicy()` in `src/preload_policy.hpp`):**  
  * **`WholeAlbum` (`>= 16 GiB` RAM, default on 32 GB rig):** Loads tracks into `RamFilePreloader` up to an `8 GiB` cap (`8ULL * 1024ULL * 1024ULL * 1024ULL`).  
  * **`Window3Files` (`6.5 GiB` to `< 16 GiB` RAM):** Maintains a sliding 3-track window in RAM (`2 GiB` cap), evicting completed entries behind `current_index`.  
  * **`SingleFile` (`< 6.5 GiB` RAM or `< 2 GiB MemAvailable`):** Restricts preloading to a `512 MiB` cap (`64 MiB` on `<= 1.5 GiB` handhelds).  
* **Stream Decoding (`FlacRingDecoder` & `DsdDopStreamer`):** `PersistentDecoderWorker` refreshes the preload window on Core 5 (`preloader.RefreshWindow(tracks_, idx)`), inspects `preloader.Find(path)`, and routes `.dsf` / `.dff` tracks through `DsdTrackSession` / `DsdDopStreamer` (`176.4 kHz` DoP for DSD64, `352.8 kHz` DoP for DSD128 via `DopPacker::PackStereoFrame`) or FLAC tracks through `FlacRingDecoder` into `StereoFrameRing` (`262,144` frames).

### Physical USB Host Controller Port Separation & Multi-DAC Isolation

* **Dedicated Audio Bus (Core 0 IRQ):** Connect the USB DAC to a dedicated xHCI root port with USB runtime autosuspend disabled (`power/control="on"`, `power/autosuspend="-1"`) via `99-ax-usb-dacs.rules` (covering **HiBy FC3** `0x32BB`/`0x2FC6`, **FiiO KA11** `0x2972`, **TRN Black Pearl** `0x3302`/`0x1A86`, **Moondrop Dawn Pro 2**, and **Apple USB-C UK A2155** `0x05AC`) and PulseAudio/PipeWire ACP claiming suppressed (`PULSE_IGNORE=1`, `ACP_IGNORE=1`).  
* **HiBy FC3 Output-Stage Revisions (`2.0 Vrms` vs. `1.3 Vrms`):** Both HiBy FC3 hardware revisions share VID `0x32BB` (`hw:CARD=FC3,DEV=0`). `EssChipProfile` and `ax-mode-cli` default to `--volume 0.80` (`fc3_2v0`) for the `2.0 Vrms` (`112 mW`) revision and support `--volume 0.95` (`fc3_1v3` / `AX_DAC_VOL=0.95` in `/userdata/system/ax-dac.conf`) for the earlier `1.3 Vrms` (`70 mW`) revision when driving TI `TPA3116` / `TPA3255` Class-D speaker amplifiers.  
* **Isolated Control / Capture Bus (Core 5):** Keep the Akai MPK Mini MK3 or V4L2 capture device (`V4l2LiveCaptureSource`) on its own port/bus so control and capture I/O do not contend with Core 0 audio submission.

---

## 3\. Internal Subsystems (`src/` & `CMakeLists.txt`)

1. **Build Targets (`CMakeLists.txt`):** Builds `flac_ax` (`src/main.cpp`) plus configurable `ax_av_studio` (`FLAC_AX_BUILD_AV_STUDIO=ON`), `ax_video_upscaler` (`FLAC_AX_BUILD_VIDEO_UPSCALER=ON`), and `ax_appliance_tui` (`FLAC_AX_BUILD_APPLIANCE_TUI=ON`), linking `ax::core` (`ax-sdk v1_36_2` via `find_package(ax-sdk)` with `FetchContent` fallback), `FLAC++` / `FLAC`, `asound`, and optional `SDL2` \+ `OpenGL` (`FLAC_AX_ENABLE_SDL2_GL` / `FLAC_AX_HAS_SDL2_GL`).  
2. **ESS Sabre & Multi-DAC Hardware Hooks, DoP Packer & Mixer (`src/ess_dac_hooks.hpp` & `src/dsd_dop_reader.hpp`):**  
   * Resolves `ES9281AC Pro` (`2.0 Vrms` `fc3_2v0` and `1.3 Vrms` `fc3_1v3`) and `ES9281PRO` profiles (`MakeEssProfile`, `ResolveEssChipHook`), recommended load volumes (`RecommendedVolumeForTarget` for `Speakers`, `SpeakersFc3_1v3`, `GradoSr325x`, `HeadphonesHighZ`, `HeadphonesClosed`, and `LowOhmIem`), sample rate negotiation (`NegotiateEssRate` within `[44100 Hz, 384000 Hz]`), and buffer period counts (`RecommendedBufferPeriods`: `8` at `>= 176.4 kHz`, `4` otherwise).  
   * Provides `DopPacker` (`0x05` / `0xFA` markers, `PackStereoFrame`, `PackStereoInterleaved`) and `ParseDsdMemoryHeader` / `ParseDsdFileHeader` / `DsdDopStreamer` for `.dsf` and `.dff` streams.  
   * Provides `EssHardwareMixer` (`--volume` / `--mixer-card`) to control ALSA hardware playback volume on the active card (`FC3` default).  
3. **Akai MPK Mini MK3 ALSA RawMIDI & OLED Feedback (`src/midi/mpk_mini3.hpp`, `.cpp` & `src/midi/mpk_mini3_oled.hpp`):**  
   * Auto-discovers `MPKmini3` in `/proc/asound/cards` (`hw:CARD=MPKmini3,DEV=0`), reads non-blocking ALSA RawMIDI on Core 5, parses running status \+ SysEx framing (`0xF0..0xF7`), and pushes events into a 1,024-entry power-of-two lock-free SPSC queue.  
   * `MpkMini3OledFeedback` rate-limits (`45 ms` default interval) off-RT parameter/SysEx feedback for K1–K8 (`CC 70–77`).  
4. **Synthesizer & Effects DSP (`src/dsp/full_feature_synth.hpp`, `src/dsp/stereo_reverb_dsp.hpp`, `src/dsp/tape_delay_dsp.hpp` & `src/dsp/avx2_synth.hpp/.cpp`):**  
   * `FullFeatureSynthEngine` (used by `studio_main.cpp`) runs 16 melodic voices \+ 8 drum voices, a 2-pole SVF low-pass filter \+ soft saturation, `StereoReverbDsp`, `TapeDelayDsp`, and a 1024-point Hann-windowed radix-2 FFT \+ 256-sample stereo scope into double-buffered `VisualSnapshot` slots.  
   * `PolySynthEngine` (`src/dsp/avx2_synth.hpp/.cpp`) provides the standalone 16-voice saw+sub \+ 2-pole SVF \+ 1024-pt FFT engine.  
5. **720p/1080p/2K Video Upscaler & Audio-Master Sync (`src/video/video_upscaler_main.cpp` & `src/video/*.hpp`):**  
   * Resolves hardware guardrails via `Resolve720pGuardrails` (`5.50 GiB` VRAM ceiling, `5.00 GiB` reap trigger, `0x20` storage/aux mask).  
   * Captures live retro video on Core 5 via `V4l2LiveCaptureSource` (`YUYV` / `MJPEG` MMAP) using double-buffered `FrameSlot` entries protected by per-slot atomic sequence counters (`std::atomic<uint64_t> seq`) and `last_consumed_seq_` so Core 1 never reads a torn frame.  
   * Paces frames via `AudioMasterPtsSync` (`AudioMasterDvd` / `LiveLowLatency`) against the ESS audio clock, and runs `Core1UpscaleStage` on Core 1 with OpenGL 3.3 Core shaders (`kUpscale720pFragmentShader`), fullscreen triangle VAO, and planar `Y` (`GL_R8`) \+ `UV` (`GL_RG8`) texture uploads (`240p`, `bob`, `dvd` scan modes).  
6. **Core 5 Coffee/Espresso Appliance TUI (`src/appliance_tui_main.cpp` & `src/tui/appliance_tui.hpp`):**  
   * Zero-dependency 24-bit ANSI TrueColor TUI pinned to Core 5 (`0x20`) matching the Piccolo-AX Espresso palette (`#0B0806` background, `#C88A55` coffee accent, `#F4EDE4` cream text), displaying live `/run/lock/ax-dac.lock` state, RAM/thermal telemetry, and clean mode handoff to `launch-desktop.sh`.  
7. **Shared DAC Arbitration (`src/studio_main.cpp`, `src/main.cpp`, `src/video/video_upscaler_main.cpp`):** Coordinates exclusive access to the active DAC via `/run/lock/ax-dac.lock` (`flock(LOCK_EX | LOCK_NB)`) and `SIGINT`/`SIGTERM`/`SIGUSR1` handlers (triggered automatically by `axGenerator.py` on emulator launch).

---

## 4\. Wider Silicon Preservation Society Repo Topology

`FLAC_AX` integrates with `ax-sdk` (`v1_36_2`), `Piccolo-AX` (`batocera-ax-external`), `piccolo-ax-desktop`, `Generation-X-AX`, `Cemu-Latte-AX`, and `Eden-Espresso-Wave-AX` across the Silicon Preservation Society ecosystem, sharing `ax-sdk` profiles/core topology, multi-DAC `udev` isolation (`99-ax-usb-dacs.rules`), and `/run/lock/ax-dac.lock` DAC arbitration.

---

## 5\. Current Status & Next Steps

### Delivered in Current Tree

* All four build targets wired in `CMakeLists.txt` (`flac_ax`, `ax_av_studio`, `ax_video_upscaler` at `src/video/video_upscaler_main.cpp`, and `ax_appliance_tui` at `src/appliance_tui_main.cpp`) with `FLAC_AX_ENABLE_SDL2_GL` / `FLAC_AX_HAS_SDL2_GL`.  
* Thread-safe lock-free `FrameSlot` seqlock handoff (`Core 5` \-\> `Core 1`) in `src/video/v4l2_live_capture.hpp` and validated `ParseDouble` / `ParseUint32` CLI parsing across `main.cpp`, `studio_main.cpp`, and `video_upscaler_main.cpp`.  
* Complete OpenGL 3.3 Core shader compilation (`kUpscale720pFragmentShader`) and planar `Y` (`GL_R8`) \+ `UV` (`GL_RG8`) texture upload pipeline in `Core1UpscaleStage::RenderFrame` (`src/video/video_upscaler_main.cpp`).  
* Both HiBy FC3 revisions (`2.0 Vrms` `fc3_2v0` `--volume 0.80` and early `1.3 Vrms` `fc3_1v3` `--volume 0.95`) supported in `src/ess_dac_hooks.hpp`, `docs/DACS.md`, and `Piccolo-AX` (`package/flac-ax/`, `99-ax-usb-dacs.rules`, and `ax-mode-cli`).

### Next Steps (Hardware Validation on Target Silicon)

1. **Compile & Link Smoke Test:** Run `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build -j 4` on the build host to verify clean compilation across all four targets.  
2. **Bare-Metal `6.12-rt` Hardware Gate:** Boot the `Piccolo-AX` image on the i5-9600K / GTX 980 Ti / HiBy FC3 bench and run `/usr/bin/verify_ax_appliance.sh` and `ax_doctor.sh` (`nohz_full=0-4`, `0` xruns at `96` frames / `2.00 ms`, and `/run/lock/ax-dac.lock` handoff).  
3. **Live V4L2 MS2130S Capture Validation:** Test `ax_video_upscaler --mode retro --preset 1080p --v4l2 /dev/video0 --scan bob` against live HDMI capture input to verify `AudioMasterPtsSync` drift (`av_delta_us`).
