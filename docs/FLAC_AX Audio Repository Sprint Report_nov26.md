# 📋 Sprint Progress Report — `FLAC_AX` Bit-Perfect ALSA Pipeline, `[AX]` Audio/Visual Studio & Single-Stream Video Upscaler

## 🎯 Executive Summary

This sprint finalized the `FLAC_AX` bit-perfect playback runtime (`preload_policy.hpp`, `ess_dac_hooks.hpp`, and `main.cpp`), expanded `FLAC_AX` into the unified **`[AX]` Audio/Visual Studio & Appliance Layer** (`ax_av_studio`), and added the single-stream **`ax_video_upscaler`** (`720p` / `1080p` / `2K`) DVD & live retro console HDMI capture pipeline (`MacroSilicon MS2130S`). Across all three binaries, the system unites **Akai MPK Mini MK3** ALSA RawMIDI control & **MS2130S** V4L2 capture (`Core 5`), a **16-voice polyphonic synthesizer \+ 64-band FFT analyzer** (`Cores 2–4`), direct **`SCHED_FIFO 80` ESS Sabre (`ES9281AC Pro`) `S32_LE` ALSA output & audio-master PTS sync** (`Core 0`), and a **VBlank-locked GTX 980 Ti OpenGL Studio HUD & video scaler** (`Core 1`) on the **i5-9600K \+ 32 GB DDR4 \+ GTX 980 Ti \+ HiBy FC3** hardware baseline.

---

## ✅ Completed Deliverables

### 1\. Host RAM Preload Policy & Zero-Disk Stream Decoding (`src/preload_policy.hpp` & `src/main.cpp`)

* **Linux `/proc/meminfo` Detection (`ReadLinuxMemInfo` & `DetectHostPreloadPolicy`):** Implemented direct parsing of `MemTotal` and `MemAvailable` to classify host memory tiers without external dependencies.  
* **Adaptive RAM Preload Tiers (`EvaluatePreloadPolicy`):**  
  * `WholeAlbum` (`>= 16 GiB` total RAM, default on the `32 GB` i5-9600K baseline): Preloads the full album queue into memory with an `8 GiB` safety ceiling.  
  * `Window3Files` (`>= 6.5 GiB` and `< 16 GiB` total RAM): Maintains a sliding 3-track preload window with a `2 GiB` cap and automatic fallback to `SingleFile` if `MemAvailable < 2 GiB`.  
  * `SingleFile` (`< 6.5 GiB` total RAM): Loads one track at a time within a `512 MiB` ceiling.  
* **Zero-Disk In-Memory Decoding (`FlacMemoryOrFileDecoder` & `PersistentDecoderWorker`):** Replaced per-track thread creation with a persistent worker pinned to Core `5` (`storage_aux`) decoding directly from `RamFilePreloader::Entry::bytes` via `FLAC::Decoder::Stream` callbacks.

### 2\. ESS Sabre DAC Hooks & `ax-sdk` Profile Verification (`src/ess_dac_hooks.hpp`)

* **Chip Profile Resolution (`ResolveEssChipHook` & `MakeEssProfile`):** Added explicit hardware profiles for **ES9281AC Pro** (`HiBy FC3`, `hw:CARD=FC3,DEV=0`) and **ES9281PRO**, enforcing `SND_PCM_FORMAT_S32_LE`, direct hardware sample rates (`44.1 kHz`–`384 kHz`), `96`\-frame default ALSA periods (`128`\-frame fallback), and auto-scaled buffer multipliers (`4x` / `8x` at `>= 176.4 kHz`).  
* **Rate Negotiation & Hardware State Validation (`NegotiateEssRate` & `OnEssHardwareConfigured`):** Clamps stream rates to hardware bounds, disables ALSA software resampling (`snd_pcm_hw_params_set_rate_resample(0)`), and verifies `SND_PCM_STATE_PREPARED` / `SND_PCM_STATE_RUNNING`.  
* **Schema `v2.1` Matrix Cross-Check (`CrossCheckHardwareProfiles`):** Added startup inspection of `ax-sdk/config/hardware_profiles.json` to verify `"schema_version": "2.1"`, `"Audio|output_device": "auto"`, and CPU profile matching (`i5-9600K`).

### 3\. Bit-Perfect Playback Engine & Core Isolation (`src/main.cpp`)

* **SPSC Ring & `ax-sdk` (`v1_36_1`) Integration (`StereoFrameRing`):** Implemented a 64-byte cache-line-aligned `65,536`\-frame lock-free stereo ring buffer backed by `ax::audio::LockFreeStereoRing` (`FLAC_AX_HAS_AX_SDK`), with `/dev/shm/ax_telemetry` callback jitter export (`min`/`p99`/`max` µs and ring low-water marks).  
* **Bit-Perfect `S32_LE` Conversion & Startup Watermark (`SampleToS32` & `WaitForStartupWatermark`):** Left-shifts 16-bit and 24-bit FLAC samples cleanly into 32-bit signed integers (`S32_LE`) and waits for a `16,384`\-frame (`~341 ms @ 48 kHz`) pre-buffer watermark before starting or re-locking ALSA output.  
* **RT Memory Hardening & `2:3:1` Core Pinning:** Added `mlockall(MCL_CURRENT | MCL_FUTURE)` at startup and `ApplyThreadAffinityAndPriority` promoting the Core `0` ALSA writer to `SCHED_FIFO` priority `80` strictly inside the hot write loop.

### 4\. Unified `[AX]` Audio/Visual Studio (`ax_av_studio` — `src/midi/`, `src/dsp/`, `src/visual/`, `src/studio_main.cpp`)

* **Core `5` Akai MPK Mini MK3 ALSA RawMIDI Listener (`src/midi/mpk_mini3.hpp` & `.cpp`):** Auto-detects `MPK mini 3` via `/proc/asound/cards`, parses 25 velocity keys (Ch `1`), 8 MPC pads (`Bank A/B`, Ch `10`), 8 endless rotary encoders (`K1–K8` \= `CC 70–77`), pitch bend \+ modulation thumbstick (`CC 1`), and sustain pedal (`CC 64`) with hot-unplug recovery into a 64-byte cache-line-aligned lock-free SPSC event ring (`1,024` events).  
* **Polyphonic Synthesizer, Tactile Mixer & 64-Band FFT Engine (`src/dsp/avx2_synth.hpp` & `.cpp`):** Implemented a 16-voice saw \+ sub-oscillator synthesizer with Chamberlin 2-pole state-variable low-pass filter, soft harmonic saturation, stereo width spread, and real-time `1024`\-sample Hann-windowed radix-2 FFT \+ `256`\-sample oscilloscope double-buffered snapshots (`VisualSnapshot`).  
* **Core `1` GTX 980 Ti OpenGL Studio HUD (`src/visual/av_hud.hpp` & `.cpp`):** Built a VBlank-locked (`SDL_GL_SetSwapInterval(1)`) OpenGL studio display pinned to Core `1` (`0x02`) rendering the 64-band thermal FFT spectrum analyzer, dual-channel L/R oscilloscope \+ peak meters, 16 active voice LEDs, 2×4 MPC pad velocity grid, and 2×4 `K1–K8` rotary encoder meters under `< 64 MB` VRAM.  
* **Shared DAC Arbitration (`src/studio_main.cpp` & `CMakeLists.txt`):** Wired `/run/lock/ax-dac.lock` (`flock`) and `SIGUSR1` clean release so `ax_av_studio` yields `hw:CARD=FC3,DEV=0` immediately when `Piccolo-AX` launches an emulator (`Eden`, `Cemu`, `RMG`, `BlastEm`, `Cannonball`, `Frontier`, `Flycast`, or `Hatari`).

### 5\. Single-Stream `720p` / `1080p` / `2K` Video Upscaler (`ax_video_upscaler` — `src/video/`, `src/video_upscaler_main.cpp`, `CMakeLists.txt`)

* **Hardware Guardrails & Resolution Presets (`src/video/ax_720p_hw_gate.hpp` & `src/video_upscaler_main.cpp`):** Added `--preset <720p|1080p|2k>` (`1280x720`, `1920x1080`, `2560x1440`) governed by `hardware_profiles.json` (`v2.1`) guardrails (`vram_ceiling_gib: 5.50`, `vram_reap_trigger_gib: 5.00`, `storage_aux` mask `0x20`), including safe optional `Lossless.dll` directory probing on Windows while keeping the native `FP32` Linux shader path active.  
* **Core `5` MacroSilicon `MS2130S` V4L2 Capture (`src/video/v4l2_live_capture.hpp`):** Built a zero-lag `MMAP` capture worker pinned to Core `5` (`storage_aux`) targeting the **MacroSilicon MS2130S** (`VID:PID 534d:2130`) over USB 3.0 (`5000M`), negotiating uncompressed `YUYV 4:2:2` first with `MJPEG` fallback and newest-frame handoff (`CopyLatestFrame`) to prevent capture queue buildup.  
* **Scan-Adaptive & Anti-Ringing GLSL Pipeline (`src/video/upscale_720p_shader.hpp`):** Implemented `BT.601` YUV-to-RGB conversion, `240p` progressive passthrough, `480i`/`576i` single-field `Bob` deinterlacing, DVD adaptive cadence handling, anti-ringing neighborhood clamping, and subtle dithering.  
* **Audio-Master PTS Synchronization & CMake Target (`src/video/av_pts_sync.hpp`, `src/video_upscaler_main.cpp`, `CMakeLists.txt`):** Created `AudioMasterPtsSync` (zero-allocation atomic audio frame counter \+ clock drift estimator) anchoring DVD video PTS to the Core `0` (`SCHED_FIFO 80`) ESS Sabre `S32_LE` output clock under `/run/lock/ax-dac.lock`, and added the `FLAC_AX_BUILD_VIDEO_UPSCALER` target \+ Release LTO property to `CMakeLists.txt`.

---

## 🚀 Subsequent Steps & Carry-Over Actions

1. **Ubuntu VM & Bare-Metal `Piccolo [AX]` Verification:**  
   * Compile all three targets (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLAC_AX_USE_AX_SDK=ON -DFLAC_AX_BUILD_AV_STUDIO=ON -DFLAC_AX_BUILD_VIDEO_UPSCALER=ON && ninja -C build -j 4`), verify `./build/flac_ax` bit-perfect `S32_LE` rate lock on `/proc/asound/FC3/stream0`, validate `./build/ax_av_studio` at `48 kHz` / `96`\-frame (`2.00 ms`) period with the Akai MPK Mini MK3, and test `./build/ax_video_upscaler` across `--preset 720p|1080p|2k` with the MacroSilicon `MS2130S` (`/dev/video0`) and `/run/lock/ax-dac.lock`.  
2. **Active OpenGL `720p`/`1080p`/`2K` Shader Pass & MS2130S UAC / DVD 5.1 Audio Wiring (`src/video_upscaler_main.cpp`):**  
   * Wire the Core `1` OpenGL context (`kUpscale720pFragmentShader`) to render frames at the active preset resolution (`1280x720`, `1920x1080`, or `2560x1440`), bridge `MS2130S` `48 kHz` UAC capture audio into the Core `0` ESS Sabre output loop, and add host-side DVD 5.1 (`AC-3`/`DTS`/`LPCM`) to stereo `S32_LE` downmixing ahead of `AudioMasterPtsSync`.  
3. **Piccolo-AX Buildroot Packaging (`batocera-ax-external/package/flac-ax/`):**  
   * Add `flac-ax.mk` and `Config.in` to `Piccolo-AX` so `/usr/bin/flac_ax`, `/usr/bin/ax_av_studio`, and `/usr/bin/ax_video_upscaler` build and install into the `< 512 MB` `toram` image with USB autosuspend disabled for HiBy (`VID 0x32BB`).  
4. **Native DoP (`DSD64`/`DSD128`) & Hardware UAC2 Volume Control (`ess_dac_hooks.hpp`):**  
   * Add `.dsf` / `.dff` 24-bit DoP (`0x05` / `0xFA`) packing into `StereoFrameRing<65536>` (`176.4 kHz` / `352.8 kHz`) and bind `snd_mixer_*` to the ES9281AC Pro's internal 32-bit HyperStream digital attenuator for zero-attenuation bit-exact volume control.
