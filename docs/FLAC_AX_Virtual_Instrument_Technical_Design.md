# Technical Design Document: External Virtual Instrument & Synthesizer Integration for `FLAC_AX`

## What Hi-Fi? Style Introduction: Bringing Studio-Grade Virtual Instruments Into a Bare-Metal `2.00 ms` Rig

Forget bloated desktop DAWs, background operating-system mixers, and eye-watering price tags for dedicated hardware synthesizers. On a properly partitioned `2:3:1` Linux appliance—like `Piccolo-AX` (`Linux 6.12-rt`) running on an Intel Core `i5-9600K` paired with a `HiBy FC3` (`ES9281AC Pro`) or `TRN Black Pearl` USB DAC, a `Nobsound Q10` (`TI TPA3116`) amplifier, and `Wharfedale Diamond 9.0` nearfield speakers or `Grado SR325x` headphones—music production and synthesis feel startlingly immediate. Stripping out OS scheduler jitter and software sample-rate conversion gives you the kind of crisp, unvarnished transient attack and solid stereo imaging you normally expect from dedicated hardware, at a fraction of the cost.

Our existing `ax_av_studio` engine already delivers a rock-solid, zero-allocation real-time foundation with its 16-voice polyphonic melodic synth, 8-voice percussion engine, 2-pole stereo State-Variable Filter, `StereoReverbDsp`, `TapeDelayDsp`, and `1024`\-point Hann FFT / `256`\-sample oscilloscope HUD. Bringing six curated Linux virtual instruments—**Firefly Synth**, **Surge XT**, **Dexed**, **Amsynth**, **DISTRHO Nekobi** (or **JC303**), and **Sfizz**—into `FLAC_AX` via `ax_clap_host` and `ax_lv2_host` expands that sonic palette from classic FM bells and acid basslines to multi-engine wavetable/physical/analog textures and sampled SFZ instruments, all without sacrificing your `2:3:1` core discipline or bit-exact `S32_LE` DAC handoff. Think of your built-in synth as the ultra-tight reference instrument at the heart of the desk, and these six virtual instruments as interchangeable sound modules plugged into the same deterministic signal chain.

---

## 1\. Architectural Scope & Target Instrument Lineup

This design integrates six Linux virtual instruments sourced from `psemiletov/awesome-linux-virtual-instruments` into `FLAC_AX` (`galaxywarriordad/FLAC_AX`) across `ax_clap_host` and `ax_lv2_host`:

| Instrument | Format Target | DSP / Synthesis Role | `2:3:1` & Hardware Fit |
| :---- | :---- | :---- | :---- |
| **Firefly Synth** | CLAP (`ax_clap_host`) | Semi-modular synth \+ integrated FX | Direct validation target for cross-platform `ax_clap_host` (`FLAC_AX_BUILD_CLAP_HOST=ON`) on Linux and Windows. |
| **Surge XT** | CLAP / LV2 | Mulati-technique synth (filters, modulation, FX, MPE/OSC) | High-load DSP stress test on `Cores 2–4` (`0x1C`) for `StereoFloatRing` throughput and ring watermarks. |
| **Dexed** | LV2 / CLAP | Yamaha DX7-modeled FM synthesis | Crisp, deterministic FM voices driven by Akai MPK Mini MK3 (`MPKmini3`) MIDI input with low-jitter `S32_LE` output. |
| **Amsynth** | LV2 | Classic subtractive analog-style synthesis | Low-CPU baseline smoke test for `ax_lv2_host` and `96`\-frame ALSA sink draining. |
| **DISTRHO Nekobi / JC303** | LV2 / CLAP | Single-oscillator / 303-style acid bass & filter | Fast filter/envelope transient verification with minimal voice overhead. |
| **Sfizz** | LV2 | SFZ sample playback engine | Validates deterministic sample/instrument streaming and RAM-conscious asset staging without DAW overhead. |

---

## 2\. Core Topology, Audio Pipeline & Locking Design

### 2.1 Deterministic `2:3:1` Execution Partitioning

All plugin host sessions follow the established `2:3:1` physical core layout on the 6-core `i5-9600K` (`6C/6T`, HT disabled) baseline:

* **Core `0` (`0x01`, `SCHED_FIFO` Priority `80`):** Runs `AlsaStereoSink` draining interleaved stereo `float` frames from `StereoFloatRing` and converting to `S32_LE` (`ConvertStereoFloatToS32` / `FloatSampleToS32`) for direct ALSA output (`hw:CARD=FC3,DEV=0` or `--no-alsa` fallback), sharing USB xHCI DAC IRQ isolation and `ax::RecordAudioCallbackMetrics` telemetry hooks.  
* **Core `1` (`0x02`):** Reserved for display/HUD/upscaler presentation (`ax_av_studio` / `ax_video_upscaler`) and non-audio UI/status work so rendering never preempts Core `0`.  
* **Cores `2–4` (`0x1C`):** Pins the plugin host DSP/processing loop (`ax_clap_host` or `ax_lv2_host`), executing `process()` / LV2 `run()` blocks with `-march=skylake -mbmi -mbmi2` and LTO/ThinLTO enabled and pushing stereo blocks into `StereoFloatRing`.  
* **Core `5` (`0x20`):** Handles non-real-time I/O, MIDI/control listeners, SFZ/sample preloading or staging, and appliance TUI/launcher duties (`ax_appliance_tui`).

### 2.2 Canonical Ring Transport & Race-Free DAC Arbitration

* **Canonical Ring (`StereoSampleRing<SampleT>`):** Both `ax_clap_host` and `ax_lv2_host` use `StereoFloatRing` from `src/audio/ax_stereo_ring.hpp` (a 64-byte cache-line-aligned SPSC ring using `PushInterleaved` and `PopInterleaved`), matching `StereoFrameRing32` used by `flac_ax` so lock-free ring semantics stay uniform across the repository.  
* **RAII DAC Lock (`ScopedDacLock`):** Each host acquires `/run/lock/ax-dac.lock` via `ScopedDacLock` (`src/audio/ax_dac_lock.hpp`) before opening the ALSA PCM device. On teardown or signal (`SIGINT`, `SIGTERM`, `SIGUSR1`), `ScopedDacLock` unlocks and closes its open file descriptor without unlinking `/run/lock/ax-dac.lock`, avoiding lock-file inode races across concurrent or sequential `[AX]` processes.

---

## 3\. Host Integration, Configuration & Build Discipline

### 3.1 Plugin Lifecycle & Real-Time Rules (`ax_clap_host` & `ax_lv2_host`)

1. **Discovery & Instantiation:** `ax_clap_host` loads `.clap` shared objects via `dlopen`/`dlsym` on Linux (and `LoadLibraryA`/`GetProcAddress` on Windows) and drives the standard C-ABI sequence (`clap_entry` \-\> `clap_plugin_factory` \-\> `init` \-\> `activate` \-\> `start_processing` \-\> `process` \-\> `stop_processing` \-\> `deactivate` \-\> `destroy`). `ax_lv2_host` instantiates LV2 plugins (`lilv`) for configured URIs (`Dexed`, `Amsynth`, `Nekobi`, `Sfizz`, `Surge XT`).  
2. **Preallocated Buffers Only:** Allocate stereo block buffers, conversion scratch buffers, and ring storage prior to entering the DSP loop; never call `malloc`/`new` or acquire blocking locks inside the Core `2–4` DSP step or Core `0` `AlsaStereoSink` drain loop.  
3. **Sample Rate & Period Alignment:** Negotiate sample rate and period size (defaulting to `48000 Hz` and `96` frames / `2.00 ms` where supported by the target DAC) without silent ALSA software resampling, and honor `--device` and `--no-alsa` flags for hardware or headless/dry-run testing.

### 3.2 Build, Gain-Staging & Operational Guardrails

* **Build & Cache Discipline:** Build with CMake \+ Ninja using `-j 4` on the `i5-9600K` to preserve L3 cache headroom per active compile job, `-march=skylake -mbmi -mbmi2`, LTO/ThinLTO, and `FLAC_AX_BUILD_CLAP_HOST=ON` / `FLAC_AX_BUILD_LV2_HOST=ON`.  
* **Gain Staging & DAC Protection:** Because plugin outputs (especially multi-voice `Surge XT` or layered `Dexed` patches) can swing full-scale `[-1.0f, +1.0f]`, keep host output gain staged safely and set `--volume` according to the downstream transducer (`0.80` for `2.0 Vrms` FC3/Black Pearl into Class-D speakers, `0.95` for `1.3 Vrms` FC3, `0.72` for `2.5 Vrms` KA11, `0.55` for `38 Ω` Grado SR325x, and `0.20–0.25` for low-impedance IEMs).  
* **Graceful Handoff:** Ensure `SIGINT`, `SIGTERM`, and `SIGUSR1` stop the DSP and Core `0` drain threads cleanly, drain/close ALSA PCM cleanly, and release `ScopedDacLock` so `ax_av_studio`, `flac_ax`, `ax_video_upscaler`, or an emulator can take `/run/lock/ax-dac.lock` immediately afterward.

---

## 4\. Phased Rollout & Verification Plan

1. **Phase 1 — Smoke & Baseline Host Verification:** Run `Amsynth` and `Nekobi`/`JC303` through `ax_lv2_host` and `Firefly Synth` through `ax_clap_host` with `--no-alsa` first, then against `hw:CARD=FC3,DEV=0` at `96` frames, confirming zero underruns and clean signal teardown.  
2. **Phase 2 — FM & Sampled Instrument Validation:** Load `Dexed` (with MIDI note/CC/pitch/mod input) and `Sfizz` SFZ instruments, verifying buffer stability and clean gain staging across headphones and Class-D speakers.  
3. **Phase 3 — High-Polyphony Stress & Appliance Packaging:** Stress `Surge XT` across `Cores 2–4` (`0x1C`) while monitoring Core `0` callback metrics (`ax::RecordAudioCallbackMetrics`) and `/run/lock/ax-dac.lock` switching in `Piccolo-AX`.
