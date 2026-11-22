# 🏛️ `FLAC_AX` Technical Architecture & Ecosystem Specification

**Project:** `FLAC_AX` — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio  
**Ecosystem:** The Silicon Preservation Society (`Piccolo [AX]` / `ax-sdk` `v1_36_1` / `Generation-X-AX`)  
**Target Baseline:** Intel Core `i5-9600K` (6C/6T, HT Disabled), `32 GB` DDR4-3600, NVIDIA GTX 980 Ti (`6 GB` GM200), **HiBy FC3** (`ESS Sabre ES9281AC Pro` / `ES9281PRO`) USB DAC + **Nobsound Q10** / **Wharfedale Diamond 9.0** & **Grado SR325x**, **Akai MPK Mini MK3** (`MPK mini 3`) USB-MIDI Controller

---

## 1. System Topology & Asynchronous Clock-Domain Isolation

`FLAC_AX` is engineered as a deterministic, zero-resampling Linux audio and audio-visual runtime designed to eliminate OS mixer overhead, disk I/O jitter, PCIe bus arbitration spikes, and CPU scheduler preemption. Rather than routing audio through desktop sound servers (PulseAudio or PipeWire), it opens the hardware USB Audio Class 2.0 (`UAC 2.0`) endpoint directly via ALSA (`hw:CARD=FC3,DEV=0`) while isolating control I/O, DSP synthesis, audio clock submission, and GPU presentation onto separate physical cores.

### Physical Core Domain Mapping (`2:3:1` Core Ratio)

On the reference 6-core `i5-9600K` topology (`coffee_lake_9600k` in `ax-sdk`), `FLAC_AX` enforces strict thread affinity aligned with `ax::topology`:

| Core Mask | Logical Core(s) | Domain Role | `flac_ax` (Audiophile Mode) | `ax_av_studio` (A/V Studio Mode) |
| :--- | :--- | :--- | :--- | :--- |
| **`0x01`** | **Core 0** | `Audio / IRQ` | Pinned `SCHED_FIFO` (`80`) ALSA `snd_pcm_writei` writer + USB xHCI IRQ for the HiBy FC3 DAC (`S32_LE`, `44.1–384 kHz`). | Pinned `SCHED_FIFO` (`80`) ALSA `snd_pcm_writei` writer + USB xHCI IRQ (`S32_LE`, `48 kHz`, `96`-frame / `2.00 ms` period). |
| **`0x02`** | **Core 1** | `Vulkan / Video` | Idle (available for emulator submission). | Pinned VBlank-locked (`SDL_GL_SetSwapInterval(1)`) OpenGL Studio HUD on GTX 980 Ti (`60/120 Hz`, `< 64 MB` VRAM). |
| **`0x1C`** | **Cores 2–4** | `Emulation / DSP` | Reserved exclusively for foreground emulators (`Eden`, `Cemu`, `Generation-X-AX`). | 16-voice polyphonic saw+sub synthesizer, 8-voice drum synth, 2-pole SVF low-pass filter, `StereoReverbDsp`, `TapeDelayDsp` & `1024`-pt Hann FFT tap. |
| **`0x20`** | **Core 5** | `Housekeeping / I/O` | Pinned `SCHED_OTHER` `PersistentDecoderWorker` + `RamFilePreloader` (`FLAC::Decoder::Stream` from RAM). | Pinned `SCHED_OTHER` `MpkMini3Listener` (`snd_rawmidi_open` non-blocking reader for Akai MPK Mini MK3). |

### End-to-End Signal & Clock-Domain Diagram

```text
+---------------------------------------------------------------------------------------------------+
|                        CONTROL & STORAGE DOMAIN — CORE 5 (0x20 / SCHED_OTHER)                     |
|                                                                                                   |
|  [Akai MPK Mini MK3] --(USB 1.1 FS / 1ms poll)--> [MpkMini3Listener (ALSA RawMIDI)]               |
|   25 Keys (Ch 1) | 8 Pads (Ch 10) | K1-K8 (CC 70-77) | Pitch/Mod (CC 1) | Sustain (CC 64)         |
|                                                           |                                       |
|                                                           v  Lock-Free SPSC Event Ring (1024)     |
|  [NVMe / EROFS toram] --> [RamFilePreloader] ---> [FlacMemoryOrFileDecoder (FLAC::Decoder::Stream)]|
+-----------------------------------------------------------|---------------------------------------+
                                                            |
                                                            v
+---------------------------------------------------------------------------------------------------+
|                        DSP & MIXING DOMAIN — CORES 2–4 (0x1C / nohz_full / AVX2+FMA)              |
|                                                                                                   |
|  [PolySynthEngine: 16 Voices + SVF Filter + Saturation] + [1024-pt Hann Radix-2 FFT Tap]          |
|          |                                                              |                         |
|          | Lock-Free StereoFrameRing<65536>                             | Double-Buffered         |
|          | (64-byte Cache-Line Aligned, S32_LE)                         | VisualSnapshot          |
+----------|--------------------------------------------------------------|-------------------------+
           |                                                              |
           v                                                              v
+----------------------------------------------------+  +-------------------------------------------+
|   AUDIO CLOCK DOMAIN — CORE 0 (0x01 / FIFO 80)     |  |  VIDEO CLOCK DOMAIN — CORE 1 (0x02)       |
|                                                    |  |                                           |
|  [ALSA Direct hw:CARD=FC3,DEV=0]                   |  |  [GTX 980 Ti (GM200) OpenGL Studio HUD]   |
|   - S32_LE Interleaved, 2ch, Rate-Resample = 0     |  |   - VBlank FIFO Lock (60 / 120 Hz)        |
|   - 96-frame period (2.00 ms @ 48 kHz)             |  |   - 64-Band Log FFT Spectrum Analyzer     |
|   - USB xHCI IRQ pinned exclusively to Core 0      |  |   - 256-Sample L/R Oscilloscope & Meters  |
|   - Asynchronous USB Audio 2.0 Clock Slave         |  |   - 2x4 MPK3 Pads + 2x4 K1..K8 Encoders   |
|                    |                               |  |                    |                      |
|                    v                               |  |                    v                      |
|  [HiBy FC3 / ESS Sabre ES9281AC Pro]               |  |  [HDMI 2.0 / DP 1.2 Display Output]       |
|   - 32-Bit HyperStream II Quad DAC                 |  |   - Zero-tearing studio telemetry HUD     |
|   - Time Domain Jitter Eliminator                  |  |   - < 64 MB VRAM Maxwell Shield           |
+----------------------------------------------------+  +-------------------------------------------+
```

---

## 2. Memory, Storage & USB Bus Engineering

### Memory & Storage Topology (`toram` / `tmpfs` / `mlockall`)

Because `Piccolo [AX]` runs from a read-only `EROFS` image loaded into RAM (`toram`) to preserve USB/NVMe flash media and prevent mid-playback I/O stalls:

* **Process Page Locking (`mlockall(MCL_CURRENT | MCL_FUTURE)`):** Executed at startup in both `flac_ax` and `ax_av_studio` so real-time audio ring buffers, synth voice tables, and ALSA PCM period buffers never incur minor/major page faults on Core `0`.
* **Adaptive RAM Preload Tiers (`DetectHostPreloadPolicy()` in `src/preload_policy.hpp`):**
  * **`WholeAlbum` (`>= 16 GiB` RAM, default on 32 GB rig):** Loads the entire album queue into `RamFilePreloader` up to an `8 GiB` safety ceiling.
  * **`Window3Files` (`6.5 GiB` to `< 16 GiB` RAM):** Maintains a sliding 3-track lookahead window in RAM (`2 GiB` cap), automatically evicting completed tracks behind `current_index`.
  * **`SingleFile` (`< 6.5 GiB` RAM or `< 2 GiB` `MemAvailable`):** Restricts preloading to the active track (`512 MiB` cap) to prevent out-of-memory pressure.
* **Zero-Disk Stream Decoding (`FlacMemoryOrFileDecoder`):** Subclasses `FLAC::Decoder::File` and overrides `read_callback`, `seek_callback`, `tell_callback`, `length_callback`, and `eof_callback` to stream directly from `RamFilePreloader::Entry::bytes` in DDR4 RAM with zero filesystem reads during active playback.

### Physical USB Host Controller Port Separation

To prevent USB 1.1 Full-Speed (`12 Mbps`) `1 kHz` SOF polling frames from the **Akai MPK Mini MK3** from contending with USB 2.0 High-Speed (`480 Mbps`) asynchronous isochronous audio transfers to the **HiBy FC3** on a shared xHCI Transaction Translator (TT):

* **Dedicated Audio Bus (`Core 0` IRQ):** Connect the **HiBy FC3** to a rear-panel Intel Z390 PCH USB 3.1 Gen2/Gen1 port on its own xHCI root port/IRQ, with USB runtime autosuspend disabled (`power/control=on` for HiBy `VID 0x32BB`).
* **Isolated Control Bus (`Core 5` Poll):** Connect the **Akai MPK Mini MK3** to a separate front-panel or secondary controller port so `1 ms` MIDI polling stays completely isolated from the audio clock domain.

---

## 3. Internal Subsystems (`src/`)

1. **ESS Sabre Hardware Hooks (`src/ess_dac_hooks.hpp`):**
   * Resolves chip profiles (`ES9281AC Pro` and `ES9281PRO`) and locks ALSA parameters to `SND_PCM_FORMAT_S32_LE`, 2-channel interleaved (`SND_PCM_ACCESS_RW_INTERLEAVED`), `snd_pcm_hw_params_set_rate_resample(0)`, `96`-frame period (`128`-frame fallback), and dynamic buffer multipliers (`4x` standard, `8x` at `>= 176.4 kHz` so hardware buffer duration never shrinks below `~2 ms`).
   * Clamps stream sample rates within `[44100 Hz, 384000 Hz]` via `NegotiateEssRate()` and validates `SND_PCM_STATE_PREPARED` / `SND_PCM_STATE_RUNNING` via `OnEssHardwareConfigured()`.
   * Cross-checks `ax-sdk/config/hardware_profiles.json` (`schema_version: "2.1"`) at startup via `CrossCheckHardwareProfiles()`.
2. **Bit-Perfect Integer Widening & SPSC Ring (`src/main.cpp`):**
   * `SampleToS32()` left-shifts native 16-bit (`<< 16`) and 24-bit (`<< 8`) decoded FLAC samples into signed 32-bit (`S32_LE`) containers without floating-point conversion or software attenuation.
   * `StereoFrameRing<65536>` wraps `ax::audio::LockFreeStereoRing` (`FLAC_AX_HAS_AX_SDK`), backed by `kStartupWatermarkFrames` (`16,384` frames / `~341 ms @ 48 kHz`) pre-buffering and `kXrunRecoveryFrames` (`4,096` frames) recovery.
3. **Akai MPK Mini MK3 ALSA RawMIDI Engine (`src/midi/mpk_mini3.hpp` & `.cpp`):**
   * Scans `/proc/asound/cards` to auto-discover `MPKmini3` (`hw:CARD=MPKmini3,DEV=0`), opens ALSA RawMIDI in `SND_RAWMIDI_NONBLOCK` mode on **Core `5`**, handles running status + SysEx framing (`0xF0..0xF7`), and recovers automatically on USB hot-unplug/replug.
   * Maps **25 Keys** (Ch `1` `NoteOn`/`NoteOff`), **8 MPC Pads** (Ch `10` Notes `36–51` across Banks A & B), **8 Endless Rotary Encoders** (`K1–K8` = `CC 70–77`), **4-Way Thumbstick** (X = `Pitch Bend`, Y = `CC 1` Mod Wheel), and **Sustain Pedal** (`CC 64`) into a 64-byte cache-line-aligned lock-free SPSC queue (`1,024` events).
4. **Polyphonic Synthesizer, Tactile Mixer & FFT Tap (`src/dsp/avx2_synth.hpp` & `.cpp`):**
   * 16-voice round-robin polyphonic oscillator engine (band-warmed sawtooth + octave sub-oscillator) feeding a 2-pole Chamberlin State-Variable Low-Pass Filter (`SVF`) and cubic soft-clipper (`FastSoftClip`), outputting `S32_LE` stereo frames at `96`-frame (`2.00 ms` @ `48 kHz`) periods.
   * Maps `K1–K8` (`CC 70–77`) directly to: `K1` Cutoff, `K2` Resonance, `K3` Attack, `K4` Release, `K5` Sub-Oscillator Mix, `K6` Harmonic Drive, `K7` Stereo Chorus Width, and `K8` Master Output Gain.
   * Computes an in-place `1024`-point Hann-windowed radix-2 Cooley-Tukey FFT every `256` frames (`5.33 ms`) alongside a `256`-sample stereo oscilloscope trace into a lock-free double-buffered `VisualSnapshot`.
5. **GTX 980 Ti OpenGL Studio HUD (`src/visual/av_hud.hpp` & `.cpp`):**
   * Pinned to **Core `1` (`0x02`)** with `SDL_GL_SetSwapInterval(1)` (VBlank FIFO lock) and `< 64 MB` VRAM footprint to honor Maxwell GM200 memory guardrails.
   * Renders top telemetry status bar (ESS Sabre lock LED, MPK3 online LED, 16 active voice LEDs), 64-band log-spaced FFT spectrum analyzer, dual-channel L/R oscilloscope + peak meters, 2×4 velocity-reactive MPC pad grid, and 2×4 `K1–K8` encoder bars.
6. **Shared DAC Arbitration (`src/studio_main.cpp`):**
   * Acquires `/run/lock/ax-dac.lock` via `flock(LOCK_EX | LOCK_NB)` and handles `SIGUSR1`/`SIGTERM` so `ax_av_studio` cleanly drains and closes `hw:CARD=FC3,DEV=0` whenever `Piccolo-AX` launches a foreground emulator.

---

## 4. Wider Silicon Preservation Society Repo Topology

`FLAC_AX` operates as the unified audio and audio-visual studio component inside the seven-repository Silicon Preservation Society network sharing `ax-sdk` (`v1_36_1`):

```text
                       +-----------------------------------+
                       |              ax-sdk               |
                       |   (Shared C++20 Core Library)     |
                       | - hardware_profiles.json (v2.1)   |
                       | - ax::topology (2:3:1 Core Masks) |
                       | - ax::audio (LockFreeStereoRing)  |
                       | - ax::telemetry (/dev/shm export) |
                       +-----------------+-----------------+
                                         |
        +--------------------------------+--------------------------------+
        |                                |                                |
        v                                v                                v
+-------------------+         +---------------------+          +---------------------+
|      FLAC_AX      |         |   Generation-X-AX   |          | Eden-Espresso-Wave  |
|  (A/V Studio &    |         | (7 Classic Engines) |          |  & Cemu-Latte-AX    |
| ESS Audio Engine) |         | RMG, BlastEm, OutRun|          | (Switch & Wii U)    |
| - flac_ax (CLI)   |         | EDuke32, Frontier,  |          | - Vulkan 1.4 / PGO  |
| - ax_av_studio    |         | Flycast & Hatari    |          | - Maxwell VRAM Guard|
+---------+---------+         +----------+----------+          +----------+----------+
          |                              |                                |
          +------------------------------+--------------------------------+
                                         |
                                         v
                       +-----------------------------------+
                       |           Piccolo [AX]            |
                       |   (< 512 MB Bare-Metal Linux)     |
                       | - Linux 6.12-rt + EROFS toram     |
                       | - batocera-ax-external Buildroot  |
                       | - Optional piccolo-ax-desktop     |
                       +-----------------------------------+
```

### Cross-Repository Integration Points

* **`ax-sdk` (`libax_core` / `v1_36_1`):** Single source of truth for CPU core masks (`audio_irq`, `emulation`, `storage_aux`), lock-free stereo ring templates (`ax::audio::LockFreeStereoRing`), shared-memory jitter telemetry (`/dev/shm/ax_telemetry`), and `hardware_profiles.json` (`v2.1`).
* **`Generation-X-AX`, `Cemu-Latte-AX` & `Eden-Espresso-Wave-AX`:** Share the same physical USB DAC (`hw:CARD=FC3,DEV=0`) and Core `0` IRQ domain on `Piccolo [AX]`. Exclusive DAC handoff is coordinated via `/run/lock/ax-dac.lock` (`flock`) and `SIGUSR1` hooks in `axGenerator.py`.
* **`Piccolo-AX` (`batocera-ax-external`):** Packages `flac_ax` and `ax_av_studio` via Buildroot (`CMake` + `Ninja`), applies udev rules disabling USB runtime autosuspend for HiBy (`VID 0x32BB`), and integrates seamlessly with both console mode and the opt-in `piccolo-ax-desktop` (`< 35 MB`) workbench.

---

## 5. Sprint Delivery History & Roadmap

| Sprint / Milestone | Focus Area | Key Features Delivered |
| :--- | :--- | :--- |
| **Sprint 1 — Core Audio Prototype** | Deterministic ALSA & ESS Pipeline (`FLAC_AX`) | Implemented `ConfigureAlsaForEss()` for direct `S32_LE` interleaved output (`96`/`128` period frames) targeting `ES9281AC Pro` / `ES9281PRO`; built `SampleToS32()` bit-exact MSB shifting and `StereoFrameRing<65536>` SPSC buffer; added `kStartupWatermarkFrames` (`16,384`) pre-buffering, multi-track rate-switching (`snd_pcm_drain()` on rate delta), and `EPIPE` xrun recovery. |
| **Sprint 2 — RAM Preloader & Zero-Disk Stream** | `toram` Memory Policy & RT Hardening (`FLAC_AX` / `ax-sdk`) | Implemented `DetectHostPreloadPolicy()`, `RamFilePreloader` (`WholeAlbum`, `Window3Files`, `SingleFile`), `FlacMemoryOrFileDecoder` (`FLAC::Decoder::Stream` from RAM), `PersistentDecoderWorker` pinned to Core `5`, `mlockall(MCL_CURRENT \| MCL_FUTURE)`, and `/dev/shm/ax_telemetry` jitter export. |
| **Sprint 3 — `[AX]` Audio/Visual Studio Layer** | MPK Mini MK3 + Poly Synth + GTX 980 Ti HUD (`ax_av_studio`) | Built Core `5` `MpkMini3Listener` (ALSA RawMIDI auto-detect + `K1–K8` `CC 70–77` + 8 pads + pitch/mod), Cores `2–4` `PolySynthEngine` (16-voice saw+sub + 2-pole SVF + `1024`-pt Hann FFT), Core `1` `AvStudioHud` (VBlank OpenGL spectrum/oscilloscope/MPK3 HUD), and `/run/lock/ax-dac.lock` DAC arbitration. |

### Next Technical Milestones

1. **Bare-Metal `Piccolo [AX]` Buildroot Package (`batocera-ax-external/package/flac-ax/`):** Build and install `/usr/bin/flac_ax`, `/usr/bin/ax_av_studio`, and `/usr/bin/ax_video_upscaler` inside `Piccolo-AX` with udev USB autosuspend rules for `VID 0x32BB`.
2. **Native DoP (`DSD64` / `DSD128`) & Hardware UAC 2.0 Volume Control:** Add `.dsf` / `.dff` 24-bit DoP (`0x05` / `0xFA`) packing into `StereoFrameRing<65536>` (`176.4 kHz` / `352.8 kHz`) and bind `snd_mixer_*` to the ES9281AC Pro's internal 32-bit HyperStream digital attenuator for zero-attenuation bit-exact volume control.
