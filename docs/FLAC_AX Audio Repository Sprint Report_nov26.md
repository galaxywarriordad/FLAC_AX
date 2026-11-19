# 📋 Sprint Progress Report — `FLAC_AX` Bit-Perfect ALSA Pipeline, Preload Policy & Build Workflow

## 🎯 Executive Summary

This sprint finalized the `FLAC_AX` prototype source files (`preload_policy.hpp`, `ess_dac_hooks.hpp`, and `main.cpp`), established the recommended 5-stage build and bare-metal verification order, and defined the hybrid Windows 10 editing + Ubuntu VM compilation + bare-metal `Piccolo [AX]` testing workflow for the **i5-9600K + 32 GB RAM + GTX 980 Ti + HiBy FC3** hardware baseline.

---

## ✅ Completed Deliverables

### 1. Host RAM Preload Policy & Queue Windowing (`src/preload_policy.hpp`)
* **Linux `/proc/meminfo` Detection (`ReadLinuxMemInfo` & `DetectHostPreloadPolicy`):** Implemented direct parsing of `MemTotal` and `MemAvailable` to classify host memory tiers without external dependencies.
* **Adaptive RAM Preload Tiers (`EvaluatePreloadPolicy`):**
  * `WholeAlbum` (`>= 16 GiB` total RAM, e.g., the `32 GB` i5-9600K baseline): Preloads the full album queue into memory with an `8 GiB` safety ceiling.
  * `Window3Files` (`>= 6.5 GiB` and `< 16 GiB` total RAM): Maintains a sliding 3-track preload window with a `2 GiB` cap and automatic fallback to `SingleFile` if `MemAvailable < 2 GiB`.
  * `SingleFile` (`< 6.5 GiB` total RAM): Loads one track at a time within a `512 MiB` ceiling.
* **Sliding/Album Cache Manager (`RamFilePreloader`):** Added `RefreshWindow` and `Find` to load `.flac` tracks into contiguous `std::vector<std::uint8_t>` buffers and evict already-played entries.

### 2. ESS Sabre DAC Hooks & `ax-sdk` Profile Verification (`src/ess_dac_hooks.hpp`)
* **Chip Profile Resolution (`ResolveEssChipHook` & `MakeEssProfile`):** Added explicit hardware profiles for **ES9281AC Pro** (`HiBy FC3`, `hw:CARD=FC3,DEV=0`) and **ES9281PRO**, enforcing `SND_PCM_FORMAT_S32_LE`, direct hardware sample rates (`44.1 kHz`–`384 kHz`), `96`-frame default ALSA periods (`128`-frame fallback), and DoP DSD capability flags.
* **Rate Negotiation & Hardware State Validation (`NegotiateEssRate` & `OnEssHardwareConfigured`):** Clamps stream rates to hardware bounds and verifies `SND_PCM_STATE_PREPARED` / `SND_PCM_STATE_RUNNING` after `snd_pcm_hw_params`.
* **Schema `v2.1` Matrix Cross-Check (`CrossCheckHardwareProfiles`):** Added startup inspection of `ax-sdk/config/hardware_profiles.json` to verify `"schema_version": "2.1"`, `"Audio|output_device": "auto"`, and CPU profile matching (`i5-9600K`).

### 3. Bit-Perfect Playback Engine & Core Isolation (`src/main.cpp`)
* **SPSC Ring & Optional `ax-sdk` Integration (`StereoFrameRing`):** Implemented a 64-byte cache-line-aligned `65,536`-frame lock-free stereo ring buffer with automatic switchover to `ax::audio::LockFreeStereoRing` when `FLAC_AX_HAS_AX_SDK` is enabled.
* **Bit-Perfect `S32_LE` Conversion & Startup Watermark (`SampleToS32` & `WaitForStartupWatermark`):** Left-shifts 16-bit and 24-bit FLAC samples cleanly into 32-bit signed integers (`S32_LE`) and waits for a `16,384`-frame (`~25%` ring / `~341 ms @ 48 kHz`) pre-buffer watermark before starting or re-locking ALSA output.
* **Dynamic Sample-Rate Reconfiguration:** Drains and reconfigures `snd_pcm_t` only when `target_rate` changes across tracks, preserving gapless playback on matching rates.
* **`2:3:1` Core Pinning (`ApplyThreadAffinityAndPriority`):** Pins the FLAC decoder/preloader thread to Core `5` (`storage_aux: 0x20`, non-RT) and the ALSA writer loop to Core `0` (`audio_irq: 0x01`, `SCHED_FIFO` priority `80`).

### 4. Step 1 Build Environment & Ubuntu VM Toolchain Setup
* **Ubuntu Build VM Provisioning:** Defined the optimal build VM configuration (`4 vCPUs`, `12–16 GB` RAM, `120–150 GB` dynamic NVMe disk) and ordered `apt` installation sequence (`build-essential`, `cmake`, `ninja-build`, `clang`, `lld`, `llvm`, `libflac++-dev`, `libasound2-dev`, `libusb-1.0-0-dev`).
* **Build & Verification Split:** Separated compilation/CLI/logic checks (Ubuntu VM) from real USB DAC timing, `SCHED_FIFO`, and USB autosuspend verification (bare-metal `Piccolo [AX]`).

---

## 🚀 Immediate Carry-Over Actions (Next Session)

1. **Execute Step 1 & Step 2 Verification (`FLAC_AX`):**
   * Run `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4` in the Ubuntu VM, verify `./build/flac_ax` CLI output, then deploy to `Piccolo [AX]` to verify `aplay -l`, `/proc/asound/FC3/stream0` (`S32_LE` rate lock), and USB autosuspend (`power/control=on` for VID `0x32BB`).
2. **True Zero-Disk Memory Decoding (`FLAC::Decoder::Stream`):**
   * Refactor `FlacStreamDecoder` from `FLAC::Decoder::File` to `FLAC::Decoder::Stream` so preloaded tracks decode directly from `RamFilePreloader::Entry::bytes` in memory with zero disk I/O during playback.
3. **RT Memory Hardening & `ax-sdk` Topology Mask Wiring:**
   * Add `mlockall(MCL_CURRENT | MCL_FUTURE)` (with unprivileged fallback) before entering `SCHED_FIFO` on Core `0`, and read `audio_irq` / `storage_aux` masks dynamically from `ax-sdk` (`topology.hpp`) across 4C, 6C, and 8C profiles.
4. **Shared USB DAC Arbitration (`flock` Handoff):**
   * Implement `/run/lock/ax-
