# **🎛️ FLAC\_AX — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio**

**FLAC\_AX** is a zero-resampling, RAM-resident C++20 audio, live synthesis, video-upscaling, and appliance-launcher runtime engineered for **Piccolo `[AX]`** (`Linux 6.12-rt` / Buildroot) and standard `x86_64` Linux workstations (`Ubuntu/Kubuntu 24.04+`) within **The Silicon Preservation Society** ecosystem.

By bypassing user-space audio servers (`PipeWire` / `PulseAudio` / `dmix`), locking decoded lossless albums and native DSD streams into physical RAM (`mlockall`), and enforcing a strict **`2:3:1` physical CPU core topology** (`coffee_lake_9600k` baseline), `FLAC_AX` turns preserved multi-core x86\_64 PCs paired with a **£29–£35 UAC 2.0 USB-C DAC** (**HiBy FC3**, **TRN Black Pearl**, **FiiO KA11**, or **Moondrop Dawn Pro 2**) into four deterministic bare-metal appliances in one unified codebase.

# **🏗️ 1\. Suite Binaries & `2:3:1` Physical Core Topology**

On the reference 6-core Intel Core i5-9600K (6C/6T, Hyper-Threading disabled, 32 GB DDR4-3600, NVIDIA GTX 980 Ti 6 GB GM200) topology, `FLAC_AX` isolates real-time audio clock submission from DSP synthesis, OpenGL rendering, and storage/MIDI/V4L2 I/O:

| Core Mask | Logical Core(s) | Domain Role | flac\_ax (Audiophile Transport) | ax\_av\_studio (Live A/V Synth) | ax\_video\_upscaler (Retro & DVD Scaler) | ax\_appliance\_tui (Espresso Launcher) |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| **`0x01`** | **Core 0** | **ESS Audio Clock / IRQ** | `SCHED_FIFO` (`80`) direct ALSA `S32_LE` (`44.1–384 kHz`) & `DoP` (`DSD64` `176.4 kHz` / `DSD128` `352.8 kHz`) writer \+ xHCI USB DAC IRQ | `SCHED_FIFO` (`80`) ALSA `S32_LE` writer (`48 kHz`, `96`\-frame / `2.00 ms` period) \+ xHCI USB DAC IRQ | `SCHED_FIFO` (`80`) ALSA `S32_LE` writer advancing `AudioMasterPtsSync` master clock | Idle (`0%` CPU; zero interference with Core 0 RT audio) |
| **`0x02`** | **Core 1** | **Video & HUD Domain** | CLI status & signal handling (`SIGINT` / `SIGTERM` / `SIGUSR1`) | VBlank-locked (`SwapInterval 1`) GTX 980 Ti OpenGL Studio HUD (`64`\-band log FFT \+ `256`\-sample stereo scope \+ pad/encoder meters) | `Core1UpscaleStage` OpenGL 3.3 Core `Y`/`UV` (`GL_R8` \+ `GL_RG8`) `240p` / `Bob` / `DVD` shader paced via `AudioMasterPtsSync` | Idle (available for foreground workload) |
| **`0x1C`** | **Cores 2–4** | **Emulation & DSP Plane** | Reserved exclusively for foreground `[AX]` emulators (`Eden`, `Cemu`, `RMG`, `BlastEm`, `Cannonball`, `Frontier`, `Flycast`, `Hatari`) | `FullFeatureSynthEngine` (`16` saw+sub melodic voices \+ `8` drum voices \+ 2-pole SVF \+ cubic soft clipper \+ `StereoReverbDsp` \+ `TapeDelayDsp` \+ `1024`\-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads | Idle (isolated `nohz_full` domain untouched) |
| **`0x20`** | **Core 5** | **Housekeeping & I/O Plane** | `SCHED_OTHER` `RamFilePreloader` (`WholeAlbum` `8 GiB` / `Window3Files` `2 GiB` / `SingleFile` `512 MiB` or `64 MiB` on `<= 1.5 GiB` handhelds) \+ `PersistentDecoderWorker` | `SCHED_OTHER` `MpkMini3Listener` (non-blocking ALSA RawMIDI `1024`\-slot SPSC ring) \+ `45 ms` `MpkMini3OledFeedback` \+ `WavBounceRecorder` | `V4l2LiveCaptureSource` (`MS2130S` `534d:2130` `YUYV`/`MJPEG` MMAP) with lock-free `FrameSlot` seqlock handoff | Pinned to Core `5`; renders 24-bit ANSI Espresso UI & probes `/run/lock/ax-dac.lock` \+ `/proc/meminfo` |

# **✨ 2\. Key Engineering Highlights**

* **Bit-Perfect `S32_LE` & Native DoP (`DSD64` / `DSD128`) Transport (`src/main.cpp`, `src/ess_dac_hooks.hpp`, `src/dsd_dop_reader.hpp`):**  
  * Disables ALSA software resampling (`snd_pcm_hw_params_set_rate_resample(0)`), locks process pages in RAM (`mlockall(MCL_CURRENT | MCL_FUTURE)`), and streams 16-bit/24-bit PCM left-shifted cleanly to `S32_LE` alongside `.dsf` / `.dff` `DSD64` (`176.4 kHz`) and `DSD128` (`352.8 kHz`) packed with alternating `0x05` / `0xFA` DoP markers (`DopPacker`).  
  * Supports both **HiBy FC3 (`ES9281AC Pro`, `VID 0x32BB`)** output-stage revisions (`2.0 Vrms / 112 mW` `fc3_2v0` at `--volume 0.80` and early `1.3 Vrms / 70 mW` `fc3_1v3` at `--volume 0.95`), plus `EssHardwareMixer` ALSA hardware attenuator presets for Class-D desktop amps (`0.80` / `0.95` / `0.72`), `Grado SR325x` (`0.55`), `HeadphonesHighZ` (`0.70`), `HeadphonesClosed` (`0.50`), and `LowOhmIem` (`0.22`).  
* **Three-Engine `2.00 ms` Subtractive Synthesizer & Effects Studio (`ax_av_studio` — `src/studio_main.cpp`, `src/dsp/`, `src/midi/`, `src/visual/`):**  
  * Zero-allocation real-time audio callback (`96` frames @ `48 kHz` \= `2.00 ms`) driven by **Akai MPK Mini MK3 (`MPKmini3`)** 25-key keybed, 16 velocity pads, pitch/mod thumbstick, sustain pedal (`CC 64`), and `K1–K8` rotary encoders (`CC 70–77`, plus `CC 78–79` reverb/delay), feeding a 4-tap `StereoReverbDsp`, `11%` stereo-offset `TapeDelayDsp`, and `< 64 MB` VRAM VBlank-locked OpenGL Studio HUD.  
* **Single-Stream `720p` / `1080p` / `2K` Retro & DVD Upscaler (`ax_video_upscaler` — `src/video/`):**  
  * Captures uncompressed `YUYV 4:2:2` (or `MJPEG`) from USB 3.0 HDMI capture dongles (**MacroSilicon MS2130S**) on Core `5` into double-buffered `FrameSlot` entries guarded by per-slot atomic sequence counters (`std::atomic<uint64_t> seq`), preventing torn reads on Core `1`.  
  * Uploads planar `Y` (`GL_R8`) and `UV` (`GL_RG8`) textures into an OpenGL 3.3 Core fullscreen triangle shader (`kUpscale720pFragmentShader`) supporting `240p` progressive passthrough, `480i`/`576i` single-field `Bob` deinterlacing, `DVD` adaptive cadence, anti-ringing clamping, and `AudioMasterPtsSync` clock locking.  
* **Core `5` Coffee/Espresso Appliance TUI & Exclusive DAC Arbitration (`ax_appliance_tui` — `src/tui/`):**  
  * Zero-dependency 24-bit ANSI TrueColor launcher matching the `Piccolo-AX` Espresso theme (`#0B0806` canvas, `#C88A55` coffee accent, `#F4EDE4` cream text), with live `/run/lock/ax-dac.lock` (`flock`) status, `/proc/meminfo` RAM telemetry, CPU thermal readout, and clean handoff to `piccolo-ax-desktop`.

# **🛠️ 3\. Quick Build (`CMake` \+ `Ninja`)**

## **Build Dependencies (Ubuntu / Debian / Buildroot)**

```
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
  cmake ninja-build pkg-config \
  libflac++-dev libasound2-dev libopenmpt-dev libsdl2-dev libgl1-mesa-dev
```

## **Configure & Compile (`-j 4` L3 Cache Discipline)**

```
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_STUDIO=ON \
  -DFLAC_AX_BUILD_UPSCALER=ON \
  -DFLAC_AX_BUILD_APPLIANCE_TUI=ON \
  -DFLAC_AX_BUILD_ENCODE_2GO=ON \
  -DFLAC_AX_ENABLE_NATIVE_OPT=ON \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

*(Note: When `ax-sdk` is not pre-installed locally via `AX_SDK_DIR` or `../ax-sdk`, CMake automatically fetches `ax-sdk` tag `v1_36_2` via `FetchContent`.)*

## 🎛️ 4. Reference Execution & Multi-DAC Gain Staging

All audio-capable binaries acquire `/run/lock/ax-dac.lock` (`flock(LOCK_EX | LOCK_NB)`) and release the DAC cleanly on `SIGUSR1`, `SIGTERM`, or `SIGINT`.

### 1. Speakers — 2.0 Vrms HiBy FC3 (112 mW) -> Nobsound Q10 (TPA3116) -> Wharfedale Diamond 9.0

```sh
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 ~/Music/FLAC_Album/*.flac
```

### 2. Speakers — Early 1.3 Vrms HiBy FC3 Revision (70 mW) -> Class-D Amp

```sh
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.95 ~/Music/FLAC_Album/*.flac
```

### 3. Speakers — TRN Black Pearl / Moondrop Dawn Pro 2 (2.0 Vrms) or FiiO KA11 (2.5 Vrms)

```sh
./build/flac_ax --device hw:CARD=BLACKPEARL,DEV=0 --mixer-card BLACKPEARL --volume 0.80 ~/Music/FLAC_Album/*.flac
./build/flac_ax --device hw:CARD=KA11,DEV=0 --mixer-card KA11 --volume 0.72 ~/Music/FLAC_Album/*.flac
```

### 4. Reference Open-Back Headphones (Grado SR325x, 38 Ohm) & High-Z Studio Cans (HD 560S, 120 Ohm)

```sh
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.55 ~/Music/FLAC_Album/*.flac
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.70 ~/Music/FLAC_Album/*.flac
```

### 5. Sensitive IEMs (16–32 Ohm, e.g., Moondrop Chu II / Truthear Hexa)

```sh
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.22 ~/Music/FLAC_Album/*.flac
```

### 6. Amiga / Atari ST Tracker Modules (`.mod`/`.xm`/`.s3m`/`.it`) via `libopenmpt` -> `S32_LE`

```sh
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 ~/Music/Mods/*.mod
```

### 7. Live 2.00 ms Synthesizer & OpenGL Studio HUD (with 24-bit WAV Bounce)

```sh
./build/ax_av_studio --fullscreen --record ~/Music/jam.wav --device hw:CARD=FC3,DEV=0 --volume 0.80
```

### 8. Single-Stream Retro HDMI Capture Upscaler (MacroSilicon MS2130S -> 1080p Bob) & DVD 2K Upscaler

```sh
./build/ax_video_upscaler --mode retro --preset 1080p --v4l2 /dev/video0 --scan bob --device hw:CARD=FC3,DEV=0 --volume 0.80
./build/ax_video_upscaler --mode dvd --preset 2k --input /userdata/dvd/movie.iso --scan dvd --device hw:CARD=FC3,DEV=0 --volume 0.80
```

### 9. Core 5 Coffee/Espresso Appliance TUI Launcher

```sh
./build/ax_appliance_tui
```

---

## 📚 5. Documentation

* **Technical Architecture & Ecosystem Specification** (`docs/Architecture.md`) — `2:3:1` CPU core topology, ESS DAC clock-domain isolation, ring buffer transport, and build targets
* **Complete User Guide & Build Manual** (`docs/FLAC_AX_AVStudio_Complete_User_Guide.md`) — End-to-end build manual, commercial software/hardware comparisons, and studio/host workflows
* **Bare-Metal Audio, AV & Hardware Master Guide (UK)** (`docs/FLAC_AX_Bare-Metal_Audio_AV_Appliance_Guide.md`) — Complete UK hardware stack, gain staging, Linux `udev`/WirePlumber setup, and `FlexASIO.toml`
* **Sub-£50 USB-C DAC Silicon Shootout** (`docs/DACS.md`) — Circuit-level benchmark of **HiBy FC3** (`2.0 Vrms` & `1.3 Vrms`), **TRN Black Pearl**, **FiiO KA11**, and **Moondrop Dawn Pro 2**
* **Enthusiast Guide to `FLAC_AX` & `AX AV Studio`** (`docs/Hi-Fi_and_Studio_Enthusiasts_Guide.md`) — Plain-English guide to whole-album RAM preloading, DoP DSD, tracker modules, and live synthesis
* **Transducer & Cabling Guide (Richer Sounds UK)** (`docs/Speakers_Headphones_and_Wiring.md`) — Nearfield bookshelf speakers (`£99–£149`), Class-D amps, headphones/IEMs, and speaker cable pairing
* **Sprint Progress Report** (`docs/FLAC_AX Audio Repository Sprint Report_251126.md`) — Detailed engineering log of completed deliverables, CLAP host integration, and build verification

---

## 🌐 6. Silicon Preservation Society Ecosystem (All 9 Repositories)

This repository is part of **The Silicon Preservation Society** (`[AX]`) ecosystem—a nine-repository bare-metal hardware preservation and deterministic runtime suite:

1. **Shared Silicon SDK (GitHub):** [galaxywarriordad/ax-sdk](https://github.com/galaxywarriordad/ax-sdk.git) — Shared dependency-free C++20 runtime (`ax::core`), `2:3:1` / `1:2:1` topology, HugeTLB fastmem, and telemetry
2. **Linux USB Console & A/V Appliance (GitHub):** [galaxywarriordad/Piccolo-AX](https://github.com/galaxywarriordad/Piccolo-AX.git) — `< 512 MB` `toram` EROFS `Linux 6.12-rt` console and A/V appliance
3. **Linux Handheld Appliance (GitHub):** [galaxywarriordad/Piccolo-2go](https://github.com/galaxywarriordad/Piccolo-2go.git) — Deterministic `aarch64` Buildroot handheld OS (`RK3326`, `1:2:1` isolation) for BATLEXP G350 & R36S
4. **Optional Desktop Tier (GitHub):** [galaxywarriordad/piccolo-ax-desktop](https://github.com/galaxywarriordad/piccolo-ax-desktop.git) — `< 42 MB` Tier A LXQt/X11 workbench isolated on Core `5`
5. **Bit-Perfect Audio, Plugin Hosts & A/V Engine (GitHub):** [galaxywarriordad/FLAC_AX](https://github.com/galaxywarriordad/FLAC_AX.git) — RAM-resident `S32_LE` / `DoP` / tracker transport, `ax_clap_host` & `ax_lv2_host`, `ax_av_studio`, `ax_video_upscaler`, and `ax_appliance_tui`
6. **Commodore Amiga Engine (GitHub):** [galaxywarriordad/Amiberry-AX](https://github.com/galaxywarriordad/Amiberry-AX.git) — Deterministic Amiga 500/1200/CD32 engine linked against `ax-sdk`
7. **Retro Multi-System Engine (GitHub):** [galaxywarriordad/Generation-X-AX](https://github.com/galaxywarriordad/Generation-X-AX.git) — Unified `[AX]` suite for RMG, BlastEm, Cannonball, EDuke32, Frontier: Elite II, Flycast, and Hatari
8. **Nintendo Switch Engine Fork (Forgejo):** [galaxywarriordad/Eden-Espresso-Wave-AX](https://git.eden-emu.dev/galaxywarriordad/Eden-Espresso-Wave-AX.git) — Deterministic Switch engine with `clang-cl` Skylake/BMI2, ThinLTO, 3-stage PGO, and QSV offload
9. **Nintendo Wii U Engine Fork (GitHub):** [galaxywarriordad/Cemu-Latte-AX](https://github.com/galaxywarriordad/Cemu-Latte-AX.git) — Deterministic Wii U engine linked against `ax-sdk`

For full core-pinning tables (`2:3:1` and `1:2:1`), cross-repository data flow, and architectural specifications, see [ax-sdk/REPO_TOPOLOGY.md](https://github.com/galaxywarriordad/ax-sdk/blob/main/REPO_TOPOLOGY.md).

See the [9-Repository Cloud Build Guide](https://github.com/galaxywarriordad/ax-sdk/blob/main/docs/CLOUD_BUILDS.md) for remote GitHub Actions build triggers and release artifact details across the `[AX]` ecosystem.

---

## 📄 7. License

Released under the **MIT License** ([LICENSE.md](LICENSE.md)).
```

