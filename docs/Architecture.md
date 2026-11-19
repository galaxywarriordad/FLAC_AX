# `FLAC_AX` Technical Architecture & Ecosystem Specification

**Project:** `FLAC_AX` — Deterministic Bit-Perfect Linux Audio Appliance Engine  
**Ecosystem:** The Silicon Preservation Society (`Piccolo [AX]` / `ax-sdk`)  
**Target Baseline:** Intel Core i5-9600K (6C/6T, HT Disabled), 32 GB DDR4-3600, NVIDIA GTX 980 Ti (6 GB), HiBy FC3 / ESS Sabre (`ES9281AC Pro` / `ES9281PRO`) USB DAC

---

## 1. System Topology & Hardware Thread Isolation

`FLAC_AX` is engineered as a deterministic, zero-resampling Linux audio playback engine designed to eliminate OS mixer overhead, disk I/O jitter, and CPU scheduler contention. Rather than routing audio through desktop sound servers (PulseAudio or PipeWire), it opens the hardware USB Audio Class 2.0 endpoint directly via ALSA (`hw:CARD=FC3,DEV=0`).

### CPU Domain Mapping (`2:3:1` Core Ratio)

On the reference 6-core `i5-9600K` topology (`coffee_lake_9600k` in `ax-sdk`), `FLAC_AX` enforces strict thread affinity aligned with `ax::topology`:

| Core Mask | Logical Core(s) | Domain Role | `FLAC_AX` & System Responsibility |
| :--- | :--- | :--- | :--- |
| **`0x01`** | **Core 0** | `Audio / IRQ` | Pinned `SCHED_FIFO` (priority `80`) ALSA PCM writer thread (`snd_pcm_writei`) and USB xHCI host controller interrupts for the HiBy FC3 DAC. |
| **`0x1E`** | **Cores 1–4** | `Emulation / Render` | Reserved exclusively for foreground emulator workloads (`Cemu-Latte-AX` / `Eden-Espresso-Wave-AX`); untouched by `FLAC_AX` background tasks. |
| **`0x20`** | **Core 5** | `Housekeeping / I/O` | Pinned `SCHED_OTHER` background thread (`ApplyThreadAffinityAndPriority(5, 0)`) hosting `FlacStreamDecoder` and `RamFilePreloader` file reads. |

### Memory & Storage Topology (`toram` / `tmpfs` Lifecycle)

Because `Piccolo [AX]` runs from a read-only `EROFS` image loaded into RAM (`toram`) to preserve USB/NVMe flash media and prevent mid-playback I/O stalls, `FLAC_AX` inspects `/proc/meminfo` (`MemTotal` and `MemAvailable`) at startup via `DetectHostPreloadPolicy()` and selects one of three RAM caching tiers:

* **`WholeAlbum` (`>= 16 GiB` RAM, default on 32 GB rig):** Loads the entire album queue into `RamFilePreloader` up to an `8 GiB` safety ceiling.
* **`Window3Files` (`6.5 GiB` to `< 16 GiB` RAM):** Maintains a sliding 3-track lookahead window in RAM (`2 GiB` cap), automatically dropping completed tracks behind `current_index`.
* **`SingleFile` (`< 6.5 GiB` RAM or `< 2 GiB` `MemAvailable`):** Restricts preloading to the active track (`512 MiB` cap) to prevent out-of-memory pressure.

---

## 2. Internal Architecture & Signal Pipeline

The `FLAC_AX` runtime decouples non-deterministic storage and decoding work from real-time hardware clocking through a lock-free Single-Producer Single-Consumer (SPSC) ring buffer.

```text
+-----------------------------------------------------------------------------------+
|                        Core 5 (0x20) — Housekeeping Domain                        |
|                                                                                   |
|  [Disk / tmpfs] ---> [RamFilePreloader] ---> [FlacStreamDecoder (libFLAC++)]      |
|                       (WholeAlbum / 3-File)   (24-bit/16-bit -> S32_LE MSB Shift) |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v  Lock-Free Push (Blocksize Frames)
                      +---------------------------------------+
                      |   StereoFrameRing<65536> (SPSC)       |
                      |   64-byte Cache-Line Aligned          |
                      |   Startup Watermark: 16,384 Frames    |
                      +-------------------+-------------------+
                                          |
                                          v  Lock-Free Pop (Period-Sized Chunks)
+-----------------------------------------------------------------------------------+
|                   Core 0 (0x01) — Real-Time Audio / IRQ Domain                    |
|                                                                                   |
|  [ALSA Writer Loop (SCHED_FIFO 80)] ---> [ConfigureAlsaForEss()]                  |
|                                           - Direct hw:CARD=FC3,DEV=0              |
|                                           - S32_LE Interleaved, 2ch               |
|                                           - Rate-Lock (44.1 kHz - 384 kHz)        |
|                                           - Period: 96 frames (128 fallback)      |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v  Isochronous USB Audio 2.0
                                  [HiBy FC3 / ESS Sabre]
```

### Key Subsystems (`src/`)

1. **ESS Sabre Hardware Hooks (`src/ess_dac_hooks.hpp`):**
   * Resolves chip profiles (`ES9281AC Pro` and `ES9281PRO`) and locks ALSA parameters to `SND_PCM_FORMAT_S32_LE`, 2-channel interleaved (`SND_PCM_ACCESS_RW_INTERLEAVED`), `96`-frame period (`128`-frame fallback), and a `4x` period ring (`384`-frame hardware buffer).
   * Clamps stream sample rates within `[44100 Hz, 384000 Hz]` via `NegotiateEssRate()` and validates `SND_PCM_STATE_PREPARED` / `RUNNING` before playback starts.
   * Cross-checks `ax-sdk/config/hardware_profiles.json` (`schema_version: "2.1"`) at startup to verify platform alignment.
2. **Bit-Perfect Integer Widening (`SampleToS32` in `src/main.cpp`):**
   * Left-shifts native 16-bit (`<< 16`) and 24-bit (`<< 8`) decoded FLAC samples into signed 32-bit (`S32_LE`) containers without floating-point conversion, dithering, or software volume attenuation—preserving bit-exact PCM integrity for the ESS Sabre DAC.
3. **Cache-Line-Isolated SPSC Ring (`StereoFrameRing<65536>`):**
   * Uses `ax::audio::LockFreeStereoRing` when compiled with `FLAC_AX_HAS_AX_SDK`, or an internal 64-byte cache-line-aligned fallback (`head_` and `tail_` padded to avoid false sharing between Core 5 and Core 0).
4. **Startup Watermark & Gapless / Rate-Switched Queue:**
   * `WaitForStartupWatermark()` blocks ALSA output until `16,384` frames (~341 ms at 48 kHz) are buffered in the ring so the `SCHED_FIFO` writer never starves on track start.
   * Between queued tracks, if `target_rate == active_rate`, PCM frames stream continuously without closing the ALSA handle. When sample rate changes (e.g., `44.1 kHz` → `96 kHz`), `FLAC_AX` executes `snd_pcm_drain()` and re-locks the hardware clock cleanly.

---

## 3. Wider Silicon Preservation Society Repo Topology

`FLAC_AX` operates as the dedicated audio appliance component inside a five-repository network sharing `ax-sdk` primitives:

```text
                       +-----------------------------------+
                       |              ax-sdk               |
                       |   (Shared C++20 Core Library)     |
                       | - hardware_profiles.json (v2.1)   |
                       | - ax::topology (2:3:1 Core Masks) |
                       | - ax::audio (LockFreeStereoRing)  |
                       | - ax::vram (5.50 GiB Shield)      |
                       +-----------------+-----------------+
                                         |
          +------------------------------+------------------------------+
          |                              |                              |
          v                              v                              v
+-------------------+         +---------------------+        +---------------------+
|      FLAC_AX      |         |    Cemu-Latte-AX    |        | Eden-Espresso-Wave  |
|  (Audio Engine)   |         |  (Wii U Emulator)   |        |  (Switch Emulator)  |
| - Direct ALSA     |         | - PGO + Clang C++20 |        | - Vulkan 1.4 Async  |
|   S32_LE / ESS    |         | - Intel iGPU Video  |        |   VRAM Reaper       |
| - RAM Preloader   |         | - Cubeb Audio Sink  |        | - Win32 ASIO / ALSA |
+---------+---------+         +----------+----------+        +----------+----------+
          |                              |                              |
          +------------------------------+------------------------------+
                                         |
                                         v
                       +-----------------------------------+
                       |           Piccolo [AX]            |
                       |   (< 300 MB Bare-Metal Linux)     |
                       | - EROFS toram + tmpfs Lifecycle   |
                       | - Split GCC (OS) / Clang (Apps)   |
                       | - nvidia-driver-legacy580         |
                       +-----------------------------------+
```

### Cross-Repository Integration Points

* **`ax-sdk` (`libax_core`):** Single source of truth for CPU core masks (`audio_irq`, `emulation`, `storage_aux`), lock-free stereo ring templates, and hardware profile metadata.
* **`Cemu-Latte-AX` & `Eden-Espresso-Wave-AX`:** Share the same physical USB DAC (`hw:CARD=FC3,DEV=0`) and Core 0 IRQ domain on `Piccolo [AX]`. Because ALSA `hw:` devices allow only one exclusive opener at a time, `FLAC_AX` and the emulators coordinate exclusive ownership of the ESS DAC during appliance mode switches.
* **`Piccolo [AX]` (`batocera-ax-external`):** Packages `FLAC_AX` via Buildroot (`CMake` + `Ninja`), applies udev rules disabling USB runtime autosuspend for HiBy (`VID 0x32BB`), and manages process lifecycle hooks (`S01piccolo-isolate` / `axGenerator.py`).

---

## 4. Sprint Delivery History & Roadmap

### Completed & In-Progress Sprint Overview

| Sprint / Milestone | Focus Area | Key Features Delivered |
| :--- | :--- | :--- |
| **Sprint 1 — Core Audio Prototype** | Deterministic ALSA & ESS Pipeline (`FLAC_AX`) | Implemented `ConfigureAlsaForEss()` for direct `S32_LE` interleaved output (`96`/`128` period frames) targeting `ES9281AC Pro` / `ES9281PRO`; built `SampleToS32()` bit-exact MSB shifting and `StereoFrameRing<65536>` SPSC buffer; added `kStartupWatermarkFrames` (`16,384`) pre-buffering, multi-track rate-switching (`snd_pcm_drain()` on rate delta), and `EPIPE` xrun recovery. |
| **Sprint 2 — RAM Preloader & Cross-Host Alignment** | `toram` Memory Policy & Ecosystem Sync (`FLAC_AX` / `ax-sdk` / `Espresso`) | Implemented `DetectHostPreloadPolicy()` and `RamFilePreloader` (`SingleFile`, `Window3Files`, `WholeAlbum` tiers from `/proc/meminfo`); wired `CrossCheckHardwareProfiles()` against `ax-sdk` `hardware_profiles.json` (`v2.1`) and optional `ax::audio::LockFreeStereoRing` linkage; aligned `Eden-Espresso-Wave-AX` and `ax-sdk` Core 0 IRQ guardrails (`2:3:1` scaling, Vulkan 1.4 async VRAM shield) and established the Windows 10 edit → Ubuntu VM build → `Piccolo [AX]` bare-metal test workflow. |

---

### Subsequent Steps to Complete

#### Step 1 (Sprint 3A) — Ubuntu VM Build Verification
* **Task:** Configure and compile `FLAC_AX` in the Ubuntu LTS build VM (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`) with `libflac++-dev` and `libasound2-dev`, verifying clean linkage both with and without `FLAC_AX_USE_AX_SDK`.
* **Verification:** `./build/flac_ax` builds with zero errors and outputs CLI usage options cleanly.

#### Step 2 (Sprint 3B) — Bare-Metal Piccolo Hardware Validation
* **Task:** Deploy `flac_ax` to bare-metal `Piccolo [AX]`, disable USB autosuspend for VID `0x32BB` (`power/control` = `on`), and run multi-rate `.flac` queues (`44.1 kHz` → `96 kHz` → `192 kHz`) while inspecting `/proc/asound/FC3/stream0`.
* **Verification:** `/proc/asound/FC3/stream0` reports `Format: S32_LE` at each track's exact native sample rate with `xruns=0`.

#### Step 3 (Sprint 4) — Zero-Disk In-Memory FLAC Decoding (`FLAC::Decoder::Stream`)
* **Task:** Replace `FLAC::Decoder::File` in `src/main.cpp` with a custom `FLAC::Decoder::Stream` subclass that reads directly from `RamFilePreloader::Entry::bytes` in memory, eliminating redundant filesystem reads after preloading.
* **Verification:** Album playback executes with zero disk reads during active playback and identical PCM frame counts.

#### Step 4 (Sprint 5) — Real-Time Memory Hardening & Dynamic Topology
* **Task:** Call `mlockall(MCL_CURRENT | MCL_FUTURE)` (with graceful fallback when `RLIMIT_MEMLOCK` is restricted) and pre-fault `out_buffer` before entering `SCHED_FIFO` priority `80`. Replace hardcoded Core `0` and Core `5` IDs with dynamic `audio_irq` and `storage_aux` masks resolved from `ax-sdk` (`4C`, `6C`, and `8C` profiles).
* **Verification:** Zero minor/major page faults (`minflt`/`majflt`) occur on the Core 0 writer thread during playback across CPU profiles.

#### Step 5 (Sprint 6) — Shared DAC Arbitration & Piccolo Buildroot Packaging
* **Task:** Add `/run/lock/ax-dac.lock` (`flock()`) and `SIGUSR1` release hooks so `FLAC_AX` cleanly drains and closes `hw:CARD=FC3,DEV=0` when `Cemu-Latte-AX` or `Eden-Espresso-Wave-AX` launches, and package `flac-ax` inside `Piccolo-AX/batocera-ax-external/package/flac-ax/`.
* **Verification:** Launching and exiting an emulator while `FLAC_AX` is active yields and reclaims the HiBy FC3 DAC without `-EBUSY` errors.
